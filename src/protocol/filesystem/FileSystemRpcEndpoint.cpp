#include <cstring>

#include <config/PlatformCompat.hpp>
#include <new>
#include <utility>

#include "diagnostics/StorageQualificationProbe.hpp"
#include "persistence/ProductConditionalMutationDigest.hpp"
#include "persistence/ProductConditionalMutationPlan.hpp"
#include "persistence/ProductFileCommitPlan.hpp"
#include "persistence/ProductTreeCleanupPlan.hpp"
#include "protocol/filesystem/FileSystemRpc.hpp"

namespace core::protocol::filesystem {

namespace conditional_mutation = core::persistence::conditional_mutation;

namespace {

static_assert(
    sizeof(core::persistence::ProductFileCommitPlan) <=
        FILESYSTEM_RPC_REQUEST_BUFFER_SIZE,
    "cooperative commit plan does not fit retained request storage"
);
static_assert(
    alignof(core::persistence::ProductFileCommitPlan) <= 8U,
    "cooperative commit plan alignment exceeds request storage"
);
static_assert(
    sizeof(conditional_mutation::ConditionalMutationPlan) <=
        FILESYSTEM_RPC_REQUEST_BUFFER_SIZE,
    "conditional mutation plan does not fit retained request storage"
);
static_assert(
    alignof(conditional_mutation::ConditionalMutationPlan) <= 8U,
    "conditional mutation plan alignment exceeds request storage"
);
static_assert(
    sizeof(core::persistence::ProductTreeCleanupPlan) <=
        FILESYSTEM_RPC_REQUEST_BUFFER_SIZE,
    "tree cleanup plan does not fit retained request storage"
);
static_assert(
    alignof(core::persistence::ProductTreeCleanupPlan) <= 8U,
    "tree cleanup plan alignment exceeds request storage"
);

bool elapsedAtLeast(uint32_t nowMs, uint32_t startedAtMs, uint32_t durationMs) {
    return static_cast<uint32_t>(nowMs - startedAtMs) >= durationMs;
}

}  // namespace

FLASHMEM FileSystemRpcEndpoint::FileSystemRpcEndpoint(
    oc::interface::ITransport& transport,
    core::persistence::ProductFileService& files,
    core::persistence::ProductDirectoryCatalog& catalog,
    NowProvider nowProvider,
    FileSystemRpcHandler::Config handlerConfig,
    MicrosProvider microsProvider
) : transport_(transport),
    files_(files),
    nowProvider_(nowProvider),
    microsProvider_(microsProvider),
    handler_(files, catalog, handlerConfig) {}

FLASHMEM FileSystemRpcEndpoint::~FileSystemRpcEndpoint() {
    end();
}

FLASHMEM void FileSystemRpcEndpoint::begin() {
    transport_.setOnReceive([this](const uint8_t* data, size_t size) {
        handleReceive_(data, size);
    });
    active_ = true;
}

FLASHMEM void FileSystemRpcEndpoint::end() {
    if (!active_) return;
    transport_.setOnReceive({});
    cancelPendingJobs_();
    active_ = false;
}

void FileSystemRpcEndpoint::advance(uint32_t nowMs, bool playbackActive) {
    if (!active_) return;

    auto& jobs = files_.persistenceJobs();
    if (upload_job_.valid() && !jobs.owns(upload_job_)) {
        // Media invalidation already removed the global record and mutation
        // lease. This call only clears the handler's stale local identity.
        handler_.abortWriteSession();
        (void)jobs.cancelAfterUnwind(upload_job_);
        upload_started_ms_ = 0U;
        for (auto& frame : pending_) {
            if (frame.uploadContinuation) {
                failStaleFrame_(frame, nowMs, FileSystemRpcStatus::STORAGE_ERROR,
                                FileSystemJobError::MEDIA_CHANGED);
            }
        }
    }

    // Media removal invalidates coordinator records without being able to
    // mutate the endpoint's move-only token copies. Reap every stale ordinary
    // frame here so a retained continuation cannot occupy a queue slot forever.
    for (auto& pending : pending_) {
        if (pending.size == 0U || pending.uploadContinuation ||
            jobs.owns(pending.token)) {
            continue;
        }
        failStaleFrame_(pending, nowMs, FileSystemRpcStatus::STORAGE_ERROR,
                        FileSystemJobError::MEDIA_CHANGED);
    }

    if (upload_job_.valid() && !uploadPromotionPending_() &&
        (elapsedAtLeast(
             nowMs,
             upload_started_ms_,
             FILESYSTEM_RPC_TOTAL_WRITE_TIMEOUT_MS
         ) ||
         handler_.writeSessionIdleExpired(nowMs) ||
         jobs.deferredAutosaveAged(nowMs))) {
        // Closing the backend stream is itself filesystem work. Preserve the
        // stopped-only containment and perform the safe unwind on the first
        // non-playback turn after the deadline.
        if (!playbackActive) advanceUploadTimeout_(nowMs);
        return;
    }

    PendingFrame* frame = activeFrame_();
    if (!frame) return;

    FileSystemRpcMessageId messageId = FileSystemRpcMessageId::WRITE_COMMIT_REQUEST;
    if (frame->operation == PendingOperation::FRAME) {
        auto decoded = FileSystemRpcCodec::decodeFrame(frame->data, frame->size);
        messageId = decoded
            ? decoded.value().messageId
            : FileSystemRpcMessageId::ERROR_RESPONSE;
    }
    auto& token = frame->uploadContinuation ? upload_job_ : frame->token;
    JobRecord* jobRecord = jobRecordForFrame_(*frame);
    if (jobRecord && prepareJobAdvance_(*frame, *jobRecord, nowMs, playbackActive)) { return; }

    if (!jobRecord && !frame->uploadContinuation && jobs.deadlineExpired(token, nowMs)) {
        sendErrorForRequest_(frame->requestId, FileSystemRpcStatus::BUSY);
        cancelFrameOperation_(*frame);
        (void)jobs.expire(token, nowMs);
        clearFrame_(*frame);
        return;
    }

    // An operation already acknowledged as pending remains retained while
    // playback is active. It performs no I/O and resumes from the same durable
    // checkpoint after playback stops.
    if (playbackActive && frame->operation != PendingOperation::FRAME) { return; }

    const auto quota = playbackActive
        ? core::persistence::PRODUCT_PERSISTENCE_QUOTA_ENDPOINT_FRAME
        : quotaFor_(*frame, messageId);
    if (!jobs.prepareAdvance(token, quota)) {
        if (!jobRecord) { sendErrorForRequest_(frame->requestId, FileSystemRpcStatus::BUSY); }
        return;
    }
    core::diagnostics::storage_qualification::setRequestId(frame->requestId);
    if (!jobs.claimAdvance(token, nowMs)) {
        core::diagnostics::storage_qualification::clearRequestId();
        if (!jobRecord) { sendErrorForRequest_(frame->requestId, FileSystemRpcStatus::BUSY); }
        return;
    }

    core::persistence::ProductPersistenceWorkUsage usage{};
    const uint32_t startedMicros = microsProvider_ ? microsProvider_() : 0U;

    oc::type::Result<size_t> response = oc::type::Result<size_t>::ok(0U);
    bool operationPending = false;
    if (playbackActive) {
        response = handler_.encodeErrorResponse(
            frame->requestId,
            FileSystemRpcStatus::BUSY,
            response_,
            sizeof(response_)
        );
    } else {
        auto measured = files_.measurePersistenceWork(usage);
        if (!measured) {
            if (jobRecord || frame->operation != PendingOperation::FRAME) {
                operationPending = true;
            } else {
                response = handler_.encodeErrorResponse(
                    frame->requestId,
                    FileSystemRpcStatus::BUSY,
                    response_,
                    sizeof(response_)
                );
            }
        } else {
            auto measurement = std::move(measured.value());
            if (frame->operation == PendingOperation::WRITE_COMMIT) {
                auto* plan = reinterpret_cast<core::persistence::ProductFileCommitPlan*>(
                    frame->data
                );
                response = handler_.advanceCooperativeWriteCommit_(
                    *plan,
                    frame->requestId,
                    frame->sessionId,
                    response_,
                    sizeof(response_)
                );
                operationPending = response && response.value() == 0U &&
                                   plan->active();
            } else if (frame->operation == PendingOperation::CONDITIONAL_MUTATION) {
                auto* plan = reinterpret_cast<
                    conditional_mutation::ConditionalMutationPlan*>(frame->data);
                response = handler_.advanceCooperativeConditionalMutation_(
                    *plan,
                    frame->requestId,
                    nowMs,
                    response_,
                    sizeof(response_)
                );
                operationPending = response && response.value() == 0U &&
                                   plan->active();
            } else if (frame->operation == PendingOperation::TREE_CLEANUP) {
                auto* plan = reinterpret_cast<
                    core::persistence::ProductTreeCleanupPlan*>(frame->data);
                response = handler_.advanceCooperativeRecursiveDelete_(
                    *plan,
                    frame->requestId,
                    response_,
                    sizeof(response_),
                    &measurement
                );
                operationPending = response && response.value() == 0U &&
                                   plan->active();
            } else if (frame->uploadContinuation &&
                       messageId == FileSystemRpcMessageId::WRITE_COMMIT_REQUEST) {
                std::memcpy(response_, frame->data, frame->size);
                auto copied = FileSystemRpcCodec::decodeFrame(
                    response_,
                    frame->size
                );
                if (!copied) {
                    response = handler_.encodeErrorResponse(
                        frame->requestId,
                        FileSystemRpcStatus::INVALID_MESSAGE,
                        response_,
                        sizeof(response_)
                    );
                } else {
                    auto* plan = ::new (static_cast<void*>(frame->data))
                        core::persistence::ProductFileCommitPlan{};
                    frame->operation = PendingOperation::WRITE_COMMIT;
                    response = handler_.beginCooperativeWriteCommit_(
                        copied.value(),
                        *plan,
                        frame->sessionId,
                        response_,
                        sizeof(response_)
                    );
                    operationPending = response && response.value() == 0U &&
                                       plan->active();
                }
            } else if (
                messageId == FileSystemRpcMessageId::CONDITIONAL_REPLACE_REQUEST ||
                messageId == FileSystemRpcMessageId::CONDITIONAL_DELETE_REQUEST
            ) {
                std::memcpy(response_, frame->data, frame->size);
                auto copied = FileSystemRpcCodec::decodeFrame(
                    response_,
                    frame->size
                );
                if (!copied) {
                    response = handler_.encodeErrorResponse(
                        frame->requestId,
                        FileSystemRpcStatus::INVALID_MESSAGE,
                        response_,
                        sizeof(response_)
                    );
                } else {
                    auto* plan = ::new (static_cast<void*>(frame->data))
                        conditional_mutation::ConditionalMutationPlan{};
                    frame->operation = PendingOperation::CONDITIONAL_MUTATION;
                    response = handler_.beginCooperativeConditionalMutation_(
                        copied.value(),
                        *plan,
                        response_,
                        sizeof(response_)
                    );
                    operationPending = response && response.value() == 0U &&
                                       plan->active();
                }
            } else if (messageId == FileSystemRpcMessageId::DELETE_REQUEST) {
                std::memcpy(response_, frame->data, frame->size);
                auto copied = FileSystemRpcCodec::decodeFrame(
                    response_,
                    frame->size
                );
                if (!copied) {
                    response = handler_.encodeErrorResponse(
                        frame->requestId,
                        FileSystemRpcStatus::INVALID_MESSAGE,
                        response_,
                        sizeof(response_)
                    );
                } else {
                    auto* plan = ::new (static_cast<void*>(frame->data))
                        core::persistence::ProductTreeCleanupPlan{};
                    frame->operation = PendingOperation::TREE_CLEANUP;
                    response = handler_.beginCooperativeRecursiveDelete_(
                        copied.value(),
                        *plan,
                        response_,
                        sizeof(response_)
                    );
                    operationPending = response && response.value() == 0U &&
                                       plan->active();
                }
            } else {
                response = handler_.handleAdmittedFrame(
                    frame->data,
                    frame->size,
                    nowMs,
                    response_,
                    sizeof(response_),
                    &measurement
                );
            }
        }
    }

    const uint32_t finishedMicros = microsProvider_ ? microsProvider_() : startedMicros;
    usage.wallMicros = static_cast<uint32_t>(finishedMicros - startedMicros);

    const bool sessionActive = handler_.hasActiveWriteSession();
    const bool openedUpload = !frame->uploadContinuation &&
                              messageId == FileSystemRpcMessageId::WRITE_BEGIN_REQUEST &&
                              sessionActive;
    const bool uploadContinues = frame->uploadContinuation && sessionActive;
    const bool safeYield = operationPending || (!openedUpload && !uploadContinues);
    const auto finished = jobs.finishAdvance(token, usage, safeYield);

    if (!finished) {
        if (frame->operation != PendingOperation::FRAME) {
            cancelFrameOperation_(*frame);
        } else {
            handler_.abortWriteSession();
        }
        (void)jobs.cancelAfterUnwind(token);
        if (frame->uploadContinuation) {
            upload_started_ms_ = 0U;
        }
        if (jobRecord) {
            terminalizeJob_(*frame, FileSystemJobState::FAILED,
                            FileSystemJobError::RESOURCE_EXHAUSTED, nowMs);
        } else {
            sendErrorForRequest_(frame->requestId, FileSystemRpcStatus::TOO_LARGE);
        }
        clearFrame_(*frame);
        return;
    }

    if (operationPending) return;

    if (openedUpload) {
        upload_job_ = std::move(frame->token);
        upload_started_ms_ = nowMs;
    } else if (frame->uploadContinuation) {
        if (!uploadContinues) {
            (void)jobs.complete(upload_job_);
            upload_started_ms_ = 0U;
        }
    } else {
        (void)jobs.complete(frame->token);
    }

    if (jobRecord) {
        terminalizeJobResponse_(*frame, static_cast<bool>(response) && response.value() > 0U,
                                response ? response.value() : 0U, nowMs);
    } else if (response && response.value() > 0U) {
        transport_.send(response_, response.value());
    }
    clearFrame_(*frame);
}

FLASHMEM bool FileSystemRpcEndpoint::active() const {
    return active_;
}

FLASHMEM void FileSystemRpcEndpoint::handleReceive_(const uint8_t* data, size_t size) {
    if (!active_ || !data || size == 0U) return;
    if (FileSystemJobRpcCodec::isJobRequestId(data[0])) {
        handleJobReceive_(data, size);
        return;
    }
    if (!FileSystemRpcCodec::isFileSystemRequestId(data[0])) return;
    if (size > FILESYSTEM_RPC_REQUEST_BUFFER_SIZE) {
        sendError_(data, size, FileSystemRpcStatus::TOO_LARGE);
        return;
    }

    auto decoded = FileSystemRpcCodec::decodeFrame(data, size);
    const bool continuation = decoded && upload_job_.valid() &&
        files_.persistenceJobs().owns(upload_job_) &&
        isUploadContinuation_(decoded.value().messageId);
    if (continuation) {
        for (const auto& pending : pending_) {
            if (pending.size != 0U && pending.uploadContinuation) {
                sendError_(data, size, FileSystemRpcStatus::BUSY);
                return;
            }
        }
    }

    // The leased schema-1 compatibility path retains exactly one frame. Job
    // frames use both coordinator slots, but an old Manager can never refill
    // the second payload slot before its first request reaches a safe yield.
    for (const auto& pending : pending_) {
        if (pending.size != 0U && pending.jobRecordIndex == JOB_RECORD_NONE) {
            sendError_(data, size, FileSystemRpcStatus::BUSY);
            return;
        }
    }

    PendingFrame* frame = emptyFrame_();
    if (!frame) {
        sendError_(data, size, FileSystemRpcStatus::BUSY);
        return;
    }

    std::memcpy(frame->data, data, size);
    frame->size = size;
    frame->requestId = decoded ? decoded.value().requestId : 0U;
    frame->uploadContinuation = continuation;
    if (continuation) return;

    const uint32_t nowMs = nowProvider_ ? nowProvider_() : 0U;
    core::diagnostics::storage_qualification::setRequestId(frame->requestId);
    auto admitted = files_.persistenceJobs().admit({
        .owner = core::persistence::ProductPersistenceJobOwner::FILESYSTEM_RPC,
        .nowMs = nowMs,
        .deadlineAfterMs = FILESYSTEM_RPC_TOTAL_WRITE_TIMEOUT_MS,
        .quota = core::persistence::PRODUCT_PERSISTENCE_QUOTA_ENDPOINT_FRAME,
    });
    core::diagnostics::storage_qualification::clearRequestId();
    if (!admitted) {
        clearFrame_(*frame);
        sendError_(data, size, FileSystemRpcStatus::BUSY);
        return;
    }
    frame->token = std::move(admitted.value());
}

FLASHMEM void FileSystemRpcEndpoint::handleJobReceive_(const uint8_t* data, size_t size) {
    const auto decoded = FileSystemJobRpcCodec::decodeRequest(data, size);
    if (!decoded) {
        const auto header = FileSystemJobRpcCodec::decodeRequestHeader(data, size);
        if (!header || header.value().command == FileSystemJobCommand::CAPABILITIES ||
            header.value().clientNonce == 0U ||
            (header.value().command != FileSystemJobCommand::START && header.value().jobId == 0U)) {
            return;
        }
        const auto error =
            header.value().innerRequestSize > FILESYSTEM_JOB_RPC_MAX_INNER_REQUEST_BYTES
                ? FileSystemJobError::RESOURCE_EXHAUSTED
                : FileSystemJobError::INVALID_MESSAGE;
        sendJobRejected_(header.value(), error, header.value().jobId);
        return;
    }

    const FileSystemJobRequest& request = decoded.value();
    const uint32_t nowMs = nowProvider_ ? nowProvider_() : 0U;
    reapExpiredJobRecords_(nowMs);
    switch (request.command) {
        case FileSystemJobCommand::CAPABILITIES: {
            FileSystemJobResponse response{};
            response.requestId = request.requestId;
            response.command = FileSystemJobCommand::CAPABILITIES;
            sendJobResponse_(response);
            return;
        }
        case FileSystemJobCommand::START: handleJobStart_(request, nowMs); return;
        case FileSystemJobCommand::POLL: {
            JobRecord* record = jobRecordForIdentity_(request.clientNonce, request.jobId);
            if (!record) {
                sendJobRejected_(request, FileSystemJobError::NOT_FOUND, request.jobId);
                return;
            }
            const uint8_t flags = fileSystemJobStateTerminal(record->state)
                                      ? FILESYSTEM_JOB_RPC_FLAG_TERMINAL_RETAINED
                                      : 0U;
            sendJobRecordResponse_(request, *record, flags);
            return;
        }
        case FileSystemJobCommand::CANCEL: {
            JobRecord* record = jobRecordForIdentity_(request.clientNonce, request.jobId);
            if (!record) {
                sendJobRejected_(request, FileSystemJobError::NOT_FOUND, request.jobId);
                return;
            }
            if (fileSystemJobStateTerminal(record->state)) {
                const uint8_t flags = (record->flags & JOB_FLAG_CANCEL_TOO_LATE) != 0U &&
                                              (record->state == FileSystemJobState::COMPLETED ||
                                               record->state == FileSystemJobState::FAILED)
                                          ? FILESYSTEM_JOB_RPC_FLAG_CANCEL_TOO_LATE
                                          : 0U;
                sendJobRecordResponse_(request, *record, flags);
                return;
            }
            PendingFrame* frame = frameForJobRecord_(jobRecordIndex_(*record));
            if (!frame) {
                record->state = FileSystemJobState::FAILED;
                record->error = FileSystemJobError::INTERNAL;
                record->terminalAtMs = nowMs;
                sendJobRecordResponse_(request, *record);
                return;
            }
            if (jobIrreversible_(*frame)) {
                record->flags |= JOB_FLAG_CANCEL_TOO_LATE;
                FileSystemJobResponse response{};
                response.requestId = request.requestId;
                response.command = FileSystemJobCommand::CANCEL;
                response.state = FileSystemJobState::PENDING;
                response.flags = FILESYSTEM_JOB_RPC_FLAG_CANCEL_TOO_LATE;
                response.clientNonce = record->clientNonce;
                response.jobId = record->jobId;
                response.retryAfterMs = FILESYSTEM_JOB_RPC_RETRY_AFTER_MS;
                sendJobResponse_(response);
                return;
            }
            if ((record->flags & JOB_FLAG_CANCEL_REQUESTED) != 0U) {
                sendJobRecordResponse_(request, *record);
                return;
            }
            if ((record->flags & JOB_FLAG_DEADLINE_REACHED) != 0U ||
                elapsedAtLeast(nowMs, record->admittedAtMs, record->deadlineMs)) {
                // Preserve first-event ordering: an already-latched cancel wins
                // if playback delays its unwind, while a cancel received only
                // after the absolute deadline cannot replace the typed timeout.
                record->flags |= JOB_FLAG_DEADLINE_REACHED;
                record->state = FileSystemJobState::PENDING;
                sendJobRecordResponse_(request, *record);
                return;
            }
            record->flags |= JOB_FLAG_CANCEL_REQUESTED;
            record->state = FileSystemJobState::CANCEL_PENDING;
            sendJobRecordResponse_(request, *record);
            return;
        }
    }
}

FLASHMEM void FileSystemRpcEndpoint::handleJobStart_(const FileSystemJobRequest& request,
                                                     uint32_t nowMs) {
    using conditional_mutation::digestEquals;
    using conditional_mutation::hashBytes;

    if (!FileSystemJobRpcCodec::isSupportedStartRequest(request.innerRequest,
                                                        request.innerRequestSize)) {
        sendJobRejected_(request, FileSystemJobError::UNSUPPORTED);
        return;
    }
    const auto inner =
        FileSystemRpcCodec::decodeFrame(request.innerRequest, request.innerRequestSize);
    if (!inner) {
        sendJobRejected_(request, FileSystemJobError::INVALID_MESSAGE);
        return;
    }

    uint8_t digest[FILESYSTEM_RPC_SHA256_SIZE] = {};
    if (!hashBytes(request.innerRequest, request.innerRequestSize, digest)) {
        sendJobRejected_(request, FileSystemJobError::INTERNAL);
        return;
    }
    if (JobRecord* retained = jobRecordForNonce_(request.clientNonce)) {
        if (retained->innerSize == request.innerRequestSize &&
            digestEquals(retained->requestDigest, digest)) {
            sendJobRecordResponse_(request, *retained, FILESYSTEM_JOB_RPC_FLAG_DUPLICATE_START);
        } else {
            sendJobRejected_(request, FileSystemJobError::CONFLICT, retained->jobId);
        }
        return;
    }

    if (files_.storageState() != core::persistence::ProductStorageState::READY) {
        sendJobRejected_(request, FileSystemJobError::STORAGE_UNAVAILABLE);
        return;
    }

    PendingFrame* frame = emptyFrame_();
    JobRecord* record = freeJobRecord_();
    if (!frame || !record) {
        sendJobRejected_(request, FileSystemJobError::RESOURCE_EXHAUSTED);
        return;
    }

    const bool uploadCommit =
        inner.value().messageId == FileSystemRpcMessageId::WRITE_COMMIT_REQUEST;
    auto& jobs = files_.persistenceJobs();
    if (uploadCommit) {
        if (!upload_job_.valid() || !jobs.owns(upload_job_) || !handler_.hasActiveWriteSession()) {
            sendJobRejected_(request, FileSystemJobError::PRECONDITION_FAILED);
            return;
        }
        for (const auto& existing : job_records_) {
            if ((existing.flags & JOB_FLAG_OCCUPIED) != 0U && existing.jobId == upload_job_.id()) {
                sendJobRejected_(request, FileSystemJobError::CONFLICT, existing.jobId);
                return;
            }
        }
        for (const auto& pending : pending_) {
            if (pending.size != 0U && pending.uploadContinuation) {
                sendJobRejected_(request, FileSystemJobError::RESOURCE_EXHAUSTED);
                return;
            }
        }
    }

    resetJobRecord_(*record);
    record->clientNonce = request.clientNonce;
    record->innerSize = static_cast<uint32_t>(request.innerRequestSize);
    record->admittedAtMs = nowMs;
    record->deadlineMs = request.totalDeadlineMs;
    record->mediaGeneration = files_.storageIdentity().mediaGeneration;
    record->state = FileSystemJobState::PENDING;
    record->error = FileSystemJobError::NONE;
    record->flags = JOB_FLAG_OCCUPIED;
    std::memcpy(record->requestDigest, digest, sizeof(record->requestDigest));

    std::memcpy(frame->data, request.innerRequest, request.innerRequestSize);
    frame->size = request.innerRequestSize;
    frame->requestId = inner.value().requestId;
    frame->jobRecordIndex = jobRecordIndex_(*record);
    frame->uploadContinuation = uploadCommit;
    if (uploadCommit) {
        record->jobId = upload_job_.id();
    } else {
        core::diagnostics::storage_qualification::setRequestId(frame->requestId);
        auto admitted = jobs.admit({
            .owner = core::persistence::ProductPersistenceJobOwner::FILESYSTEM_RPC,
            .nowMs = nowMs,
            // Job deadlines are enforced by the retained provider record so
            // an operation past its durable boundary can finish recovery-safe.
            .deadlineAfterMs = 0U,
            .quota = core::persistence::PRODUCT_PERSISTENCE_QUOTA_ENDPOINT_FRAME,
        });
        core::diagnostics::storage_qualification::clearRequestId();
        if (!admitted) {
            clearFrame_(*frame);
            resetJobRecord_(*record);
            sendJobRejected_(request, FileSystemJobError::RESOURCE_EXHAUSTED);
            return;
        }
        frame->token = std::move(admitted.value());
        record->jobId = frame->token.id();
    }

    FileSystemJobResponse response{};
    response.requestId = request.requestId;
    response.command = FileSystemJobCommand::START;
    response.state = FileSystemJobState::ACCEPTED;
    response.clientNonce = record->clientNonce;
    response.jobId = record->jobId;
    response.retryAfterMs = FILESYSTEM_JOB_RPC_RETRY_AFTER_MS;
    sendJobResponse_(response);
}

FLASHMEM void FileSystemRpcEndpoint::sendJobResponse_(const FileSystemJobResponse& response) {
    const auto encoded =
        FileSystemJobRpcCodec::encodeResponse(response, response_, sizeof(response_));
    if (encoded && encoded.value() > 0U) { transport_.send(response_, encoded.value()); }
}

FLASHMEM void FileSystemRpcEndpoint::sendJobRejected_(const FileSystemJobRequest& request,
                                                      FileSystemJobError error, uint32_t jobId) {
    FileSystemJobResponse response{};
    response.requestId = request.requestId;
    response.command = request.command;
    response.state = FileSystemJobState::REJECTED;
    response.error = error;
    response.clientNonce = request.clientNonce;
    response.jobId = jobId != 0U ? jobId : request.jobId;
    sendJobResponse_(response);
}

FLASHMEM void FileSystemRpcEndpoint::sendJobRecordResponse_(const FileSystemJobRequest& request,
                                                            const JobRecord& record,
                                                            uint8_t flags) {
    FileSystemJobResponse response{};
    response.requestId = request.requestId;
    response.command = request.command;
    response.state = record.state;
    response.error = record.error;
    response.flags = flags;
    response.clientNonce = record.clientNonce;
    response.jobId = record.jobId;
    if (record.state == FileSystemJobState::ACCEPTED ||
        record.state == FileSystemJobState::PENDING ||
        record.state == FileSystemJobState::CANCEL_PENDING) {
        response.retryAfterMs = FILESYSTEM_JOB_RPC_RETRY_AFTER_MS;
    }
    if (record.state == FileSystemJobState::COMPLETED) {
        response.progressPerMille = FILESYSTEM_JOB_RPC_MAX_PROGRESS_PER_MILLE;
        response.body = record.terminalResponse;
        response.bodySize = record.responseSize;
    }
    sendJobResponse_(response);
}

FLASHMEM void FileSystemRpcEndpoint::sendError_(
    const uint8_t* data,
    size_t size,
    FileSystemRpcStatus status
) {
    auto decoded = FileSystemRpcCodec::decodeFrame(data, size);
    sendErrorForRequest_(
        decoded ? decoded.value().requestId : 0U,
        status
    );
}

FLASHMEM void FileSystemRpcEndpoint::sendErrorForRequest_(
    uint16_t requestId,
    FileSystemRpcStatus status
) {
    auto response = handler_.encodeErrorResponse(
        requestId,
        status,
        response_,
        sizeof(response_)
    );
    if (response && response.value() > 0U) {
        transport_.send(response_, response.value());
    }
}

FLASHMEM FileSystemRpcEndpoint::PendingFrame* FileSystemRpcEndpoint::emptyFrame_() {
    for (auto& frame : pending_) {
        if (frame.size == 0U) return &frame;
    }
    return nullptr;
}

FLASHMEM FileSystemRpcEndpoint::PendingFrame* FileSystemRpcEndpoint::activeFrame_() {
    const uint32_t activeId = files_.persistenceJobs().activeJobId();
    if (activeId == 0U) return nullptr;
    for (auto& frame : pending_) {
        if (frame.size == 0U) continue;
        const uint32_t frameJobId = frame.uploadContinuation
            ? upload_job_.id()
            : frame.token.id();
        if (frameJobId == activeId) return &frame;
    }
    return nullptr;
}

FLASHMEM FileSystemRpcEndpoint::PendingFrame* FileSystemRpcEndpoint::frameForJobRecord_(
    uint8_t recordIndex) {
    if (recordIndex >= JOB_RECORD_COUNT) return nullptr;
    for (auto& frame : pending_) {
        if (frame.size != 0U && frame.jobRecordIndex == recordIndex) { return &frame; }
    }
    return nullptr;
}

FLASHMEM FileSystemRpcEndpoint::JobRecord* FileSystemRpcEndpoint::freeJobRecord_() {
    for (auto& record : job_records_) {
        if ((record.flags & JOB_FLAG_OCCUPIED) == 0U) return &record;
    }
    return nullptr;
}

FLASHMEM FileSystemRpcEndpoint::JobRecord* FileSystemRpcEndpoint::jobRecordForNonce_(
    uint32_t clientNonce) {
    if (clientNonce == 0U) return nullptr;
    for (auto& record : job_records_) {
        if ((record.flags & JOB_FLAG_OCCUPIED) != 0U && record.clientNonce == clientNonce) {
            return &record;
        }
    }
    return nullptr;
}

FLASHMEM FileSystemRpcEndpoint::JobRecord* FileSystemRpcEndpoint::jobRecordForIdentity_(
    uint32_t clientNonce, uint32_t jobId) {
    JobRecord* record = jobRecordForNonce_(clientNonce);
    return record && record->jobId == jobId ? record : nullptr;
}

FLASHMEM FileSystemRpcEndpoint::JobRecord* FileSystemRpcEndpoint::jobRecordForFrame_(
    const PendingFrame& frame) {
    if (frame.jobRecordIndex >= JOB_RECORD_COUNT) return nullptr;
    JobRecord& record = job_records_[frame.jobRecordIndex];
    return (record.flags & JOB_FLAG_OCCUPIED) != 0U ? &record : nullptr;
}

FLASHMEM uint8_t FileSystemRpcEndpoint::jobRecordIndex_(const JobRecord& record) const {
    const size_t index = static_cast<size_t>(&record - job_records_);
    return index < JOB_RECORD_COUNT ? static_cast<uint8_t>(index) : JOB_RECORD_NONE;
}

FLASHMEM void FileSystemRpcEndpoint::resetJobRecord_(JobRecord& record) { record = {}; }

FLASHMEM void FileSystemRpcEndpoint::reapExpiredJobRecords_(uint32_t nowMs) {
    for (auto& record : job_records_) {
        if ((record.flags & JOB_FLAG_OCCUPIED) == 0U || !fileSystemJobStateTerminal(record.state)) {
            continue;
        }
        if (!FileSystemJobRpcCodec::terminalRetained(nowMs, record.terminalAtMs)) {
            resetJobRecord_(record);
        }
    }
}

FLASHMEM bool FileSystemRpcEndpoint::jobIrreversible_(const PendingFrame& frame) const {
    if (frame.operation == PendingOperation::WRITE_COMMIT) {
        const auto* plan =
            reinterpret_cast<const core::persistence::ProductFileCommitPlan*>(frame.data);
        return plan->mapped() || plan->requiresRecoveryOnFailure();
    }
    if (frame.operation == PendingOperation::CONDITIONAL_MUTATION) {
        const auto* plan =
            reinterpret_cast<const conditional_mutation::ConditionalMutationPlan*>(frame.data);
        return plan->irreversible();
    }
    if (frame.operation == PendingOperation::TREE_CLEANUP) {
        const auto* plan =
            reinterpret_cast<const core::persistence::ProductTreeCleanupPlan*>(frame.data);
        return plan->canonicalHidden();
    }
    return false;
}

FLASHMEM bool FileSystemRpcEndpoint::advanceJobInterruption_(PendingFrame& frame, JobRecord& record,
                                                             uint32_t nowMs, bool playbackActive) {
    const bool cancelRequested = (record.flags & JOB_FLAG_CANCEL_REQUESTED) != 0U;
    const bool deadlineReached = (record.flags & JOB_FLAG_DEADLINE_REACHED) != 0U;
    if (!cancelRequested && !deadlineReached) return false;
    if (jobIrreversible_(frame)) return false;
    if (playbackActive) return true;

    auto& jobs = files_.persistenceJobs();
    auto& token = frame.uploadContinuation ? upload_job_ : frame.token;
    if (!jobs.prepareAdvance(
            token,
            core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
        )) {
        return true;
    }
    core::diagnostics::storage_qualification::setRequestId(frame.requestId);
    if (!jobs.claimAdvance(token, nowMs)) {
        core::diagnostics::storage_qualification::clearRequestId();
        return true;
    }

    core::persistence::ProductPersistenceWorkUsage usage{};
    const uint32_t startedMicros = microsProvider_ ? microsProvider_() : 0U;
    auto measured = files_.measurePersistenceWork(usage);
    if (!measured) {
        const uint32_t finishedMicros = microsProvider_ ? microsProvider_() : startedMicros;
        usage.wallMicros = static_cast<uint32_t>(finishedMicros - startedMicros);
        (void)jobs.finishAdvance(token, usage, false);
        return true;
    }
    {
        auto measurement = std::move(measured.value());
        if (frame.operation != PendingOperation::FRAME) {
            cancelFrameOperation_(frame);
        } else if (frame.uploadContinuation) {
            handler_.abortWriteSession();
        }
    }
    const uint32_t finishedMicros = microsProvider_ ? microsProvider_() : startedMicros;
    usage.wallMicros = static_cast<uint32_t>(finishedMicros - startedMicros);
    const auto finished = jobs.finishAdvance(token, usage, true);
    (void)jobs.cancelAfterUnwind(token);
    if (frame.uploadContinuation) upload_started_ms_ = 0U;

    if (!finished) {
        terminalizeJob_(frame, FileSystemJobState::FAILED, FileSystemJobError::RESOURCE_EXHAUSTED,
                        nowMs);
    } else if (cancelRequested) {
        terminalizeJob_(frame, FileSystemJobState::CANCELLED, FileSystemJobError::CANCELLED, nowMs);
    } else {
        terminalizeJob_(frame, FileSystemJobState::FAILED, FileSystemJobError::DEADLINE_EXCEEDED,
                        nowMs);
    }
    clearFrame_(frame);
    return true;
}

FLASHMEM bool FileSystemRpcEndpoint::prepareJobAdvance_(PendingFrame& frame, JobRecord& record,
                                                        uint32_t nowMs, bool playbackActive) {
    auto& jobs = files_.persistenceJobs();
    auto& token = frame.uploadContinuation ? upload_job_ : frame.token;
    if (record.mediaGeneration != files_.storageIdentity().mediaGeneration) {
        terminalizeJob_(frame, FileSystemJobState::FAILED, FileSystemJobError::MEDIA_CHANGED,
                        nowMs);
        cancelFrameOperation_(frame);
        (void)jobs.cancelAfterUnwind(token);
        if (frame.uploadContinuation) upload_started_ms_ = 0U;
        clearFrame_(frame);
        return true;
    }

    if (elapsedAtLeast(nowMs, record.admittedAtMs, record.deadlineMs) ||
        (frame.uploadContinuation &&
         elapsedAtLeast(nowMs, upload_started_ms_, FILESYSTEM_RPC_TOTAL_WRITE_TIMEOUT_MS))) {
        record.flags |= JOB_FLAG_DEADLINE_REACHED;
    }
    if (advanceJobInterruption_(frame, record, nowMs, playbackActive)) { return true; }
    return playbackActive;
}

FLASHMEM void FileSystemRpcEndpoint::terminalizeJobResponse_(PendingFrame& frame,
                                                             bool responseValid,
                                                             size_t responseSize, uint32_t nowMs) {
    // Reversible work is interrupted and terminalized before reaching this
    // point. Once a durable boundary has been crossed, the canonical provider
    // response is authoritative even if the interruption deadline was latched
    // while recovery-safe work completed.
    if (responseValid) {
        terminalizeJob_(frame, FileSystemJobState::COMPLETED, FileSystemJobError::NONE, nowMs,
                        response_, responseSize);
    } else {
        terminalizeJob_(frame, FileSystemJobState::FAILED, FileSystemJobError::INTERNAL, nowMs);
    }
}

FLASHMEM void FileSystemRpcEndpoint::failStaleFrame_(PendingFrame& frame, uint32_t nowMs,
                                                     FileSystemRpcStatus legacyStatus,
                                                     FileSystemJobError jobError) {
    if (jobRecordForFrame_(frame)) {
        terminalizeJob_(frame, FileSystemJobState::FAILED, jobError, nowMs);
    } else {
        sendErrorForRequest_(frame.requestId, legacyStatus);
    }
    cancelFrameOperation_(frame);
    clearFrame_(frame);
}

FLASHMEM void FileSystemRpcEndpoint::terminalizeJob_(PendingFrame& frame, FileSystemJobState state,
                                                     FileSystemJobError error, uint32_t nowMs,
                                                     const uint8_t* response, size_t responseSize) {
    JobRecord* record = jobRecordForFrame_(frame);
    if (!record) return;

    if (state == FileSystemJobState::COMPLETED &&
        (!response || responseSize == 0U || responseSize > JOB_TERMINAL_RESPONSE_BYTES ||
         !FileSystemJobRpcCodec::isCanonicalLegacyResponse(response, responseSize))) {
        state = FileSystemJobState::FAILED;
        error = FileSystemJobError::INTERNAL;
        response = nullptr;
        responseSize = 0U;
    }
    if (state != FileSystemJobState::COMPLETED) {
        response = nullptr;
        responseSize = 0U;
    }

    record->state = state;
    record->error = error;
    record->terminalAtMs = nowMs;
    record->responseSize = static_cast<uint8_t>(responseSize);
    record->flags &= static_cast<uint8_t>(JOB_FLAG_OCCUPIED | JOB_FLAG_CANCEL_TOO_LATE);
    if (responseSize != 0U) { std::memcpy(record->terminalResponse, response, responseSize); }
}

FLASHMEM void FileSystemRpcEndpoint::clearFrame_(PendingFrame& frame) {
    if (frame.operation == PendingOperation::WRITE_COMMIT) {
        auto* plan = reinterpret_cast<core::persistence::ProductFileCommitPlan*>(
            frame.data
        );
        plan->~ProductFileCommitPlan();
    } else if (frame.operation == PendingOperation::CONDITIONAL_MUTATION) {
        auto* plan = reinterpret_cast<
            conditional_mutation::ConditionalMutationPlan*>(frame.data);
        plan->~ConditionalMutationPlan();
    } else if (frame.operation == PendingOperation::TREE_CLEANUP) {
        auto* plan = reinterpret_cast<
            core::persistence::ProductTreeCleanupPlan*>(frame.data);
        plan->~ProductTreeCleanupPlan();
    }
    frame.size = 0U;
    frame.token = core::persistence::ProductPersistenceJobToken{};
    frame.requestId = 0U;
    frame.sessionId = 0U;
    frame.operation = PendingOperation::FRAME;
    frame.uploadContinuation = false;
    frame.jobRecordIndex = JOB_RECORD_NONE;
}

FLASHMEM void FileSystemRpcEndpoint::cancelFrameOperation_(PendingFrame& frame) {
    if (frame.operation == PendingOperation::WRITE_COMMIT) {
        auto* plan = reinterpret_cast<core::persistence::ProductFileCommitPlan*>(
            frame.data
        );
        handler_.cancelCooperativeWriteCommit_(*plan);
        plan->~ProductFileCommitPlan();
    } else if (frame.operation == PendingOperation::CONDITIONAL_MUTATION) {
        auto* plan = reinterpret_cast<
            conditional_mutation::ConditionalMutationPlan*>(frame.data);
        handler_.cancelCooperativeConditionalMutation_(*plan);
        plan->~ConditionalMutationPlan();
    } else if (frame.operation == PendingOperation::TREE_CLEANUP) {
        auto* plan = reinterpret_cast<
            core::persistence::ProductTreeCleanupPlan*>(frame.data);
        handler_.cancelCooperativeRecursiveDelete_(*plan);
        plan->~ProductTreeCleanupPlan();
    } else {
        return;
    }
    frame.operation = PendingOperation::FRAME;
}

FLASHMEM void FileSystemRpcEndpoint::cancelPendingJobs_() {
    auto& jobs = files_.persistenceJobs();
    for (auto& frame : pending_) cancelFrameOperation_(frame);
    handler_.abortWriteSession();
    for (auto& frame : pending_) {
        if (frame.token.valid()) (void)jobs.cancel(frame.token);
        clearFrame_(frame);
    }
    if (upload_job_.valid()) {
        (void)jobs.cancelAfterUnwind(upload_job_);
        upload_started_ms_ = 0U;
    }
    for (auto& record : job_records_) resetJobRecord_(record);
}

FLASHMEM void FileSystemRpcEndpoint::advanceUploadTimeout_(uint32_t nowMs) {
    auto& jobs = files_.persistenceJobs();
    if (!upload_job_.valid() || !jobs.isActive(upload_job_)) return;
    if (!jobs.prepareAdvance(
            upload_job_,
            core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
        )) {
        return;
    }
    core::diagnostics::storage_qualification::setRequestId(0U);
    if (!jobs.claimAdvance(upload_job_, nowMs)) {
        core::diagnostics::storage_qualification::clearRequestId();
        return;
    }

    core::persistence::ProductPersistenceWorkUsage usage{};
    const uint32_t startedMicros = microsProvider_ ? microsProvider_() : 0U;
    auto measured = files_.measurePersistenceWork(usage);
    if (!measured) {
        const uint32_t finishedMicros =
            microsProvider_ ? microsProvider_() : startedMicros;
        usage.wallMicros = static_cast<uint32_t>(finishedMicros - startedMicros);
        (void)jobs.finishAdvance(upload_job_, usage, false);
        return;
    }
    {
        auto measurement = std::move(measured.value());
        handler_.abortWriteSession();
    }
    const uint32_t finishedMicros = microsProvider_ ? microsProvider_() : startedMicros;
    usage.wallMicros = static_cast<uint32_t>(finishedMicros - startedMicros);
    const auto finished = jobs.finishAdvance(upload_job_, usage, true);
    if (finished) {
        (void)jobs.cancel(upload_job_);
    } else {
        (void)jobs.cancelAfterUnwind(upload_job_);
    }
    upload_started_ms_ = 0U;

    for (auto& frame : pending_) {
        if (frame.uploadContinuation) {
            sendErrorForRequest_(frame.requestId, FileSystemRpcStatus::BUSY);
            clearFrame_(frame);
        }
    }
}

FLASHMEM bool FileSystemRpcEndpoint::uploadPromotionPending_() const {
    for (const auto& frame : pending_) {
        if (frame.size != 0U && frame.uploadContinuation &&
            (frame.jobRecordIndex != JOB_RECORD_NONE ||
             frame.operation == PendingOperation::WRITE_COMMIT)) {
            return true;
        }
    }
    return false;
}

FLASHMEM bool FileSystemRpcEndpoint::isUploadContinuation_(
    FileSystemRpcMessageId messageId
) {
    return messageId == FileSystemRpcMessageId::WRITE_CHUNK_REQUEST ||
           messageId == FileSystemRpcMessageId::WRITE_COMMIT_REQUEST ||
           messageId == FileSystemRpcMessageId::WRITE_ABORT_REQUEST;
}

FLASHMEM core::persistence::ProductPersistenceWorkQuota
FileSystemRpcEndpoint::quotaFor_(
    const PendingFrame& frame,
    FileSystemRpcMessageId messageId
) {
    using namespace core::persistence;
    if (frame.operation == PendingOperation::CONDITIONAL_MUTATION) {
        const auto* plan = reinterpret_cast<const
            conditional_mutation::ConditionalMutationPlan*>(frame.data);
        switch (plan->nextWorkClass()) {
            case conditional_mutation::ConditionalPlanWorkClass::METADATA:
                return PRODUCT_PERSISTENCE_QUOTA_ASSET_METADATA;
            case conditional_mutation::ConditionalPlanWorkClass::ORDINARY_IO:
                return PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO;
            case conditional_mutation::ConditionalPlanWorkClass::PROMOTION:
            default:
                return PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE;
        }
    }
    if (frame.operation == PendingOperation::TREE_CLEANUP) {
        return PRODUCT_PERSISTENCE_QUOTA_TREE_CLEANUP;
    }
    switch (messageId) {
        case FileSystemRpcMessageId::CAPABILITIES_REQUEST:
            return PRODUCT_PERSISTENCE_QUOTA_ENDPOINT_FRAME;
        case FileSystemRpcMessageId::STAT_REQUEST:
        case FileSystemRpcMessageId::RENAME_REQUEST:
            return PRODUCT_PERSISTENCE_QUOTA_ASSET_METADATA;
        case FileSystemRpcMessageId::LIST_REQUEST:
            return PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG;
        case FileSystemRpcMessageId::READ_REQUEST:
        case FileSystemRpcMessageId::WRITE_CHUNK_REQUEST:
            return PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO;
        case FileSystemRpcMessageId::DELETE_REQUEST:
            return PRODUCT_PERSISTENCE_QUOTA_TREE_CLEANUP;
        case FileSystemRpcMessageId::WRITE_BEGIN_REQUEST:
        case FileSystemRpcMessageId::WRITE_COMMIT_REQUEST:
        case FileSystemRpcMessageId::WRITE_ABORT_REQUEST:
        case FileSystemRpcMessageId::MKDIR_REQUEST:
        case FileSystemRpcMessageId::CONDITIONAL_REPLACE_REQUEST:
        case FileSystemRpcMessageId::CONDITIONAL_DELETE_REQUEST:
        case FileSystemRpcMessageId::ERROR_RESPONSE:
        default:
            return PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE;
    }
}

}  // namespace core::protocol::filesystem

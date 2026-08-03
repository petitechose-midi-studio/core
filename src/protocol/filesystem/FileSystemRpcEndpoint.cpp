#include "protocol/filesystem/FileSystemRpc.hpp"

#include <cstring>
#include <new>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "persistence/ProductFileCommitPlan.hpp"
#include "persistence/ProductTreeCleanupPlan.hpp"
#include "protocol/filesystem/FileSystemRpcConditionalPlan.hpp"

namespace core::protocol::filesystem {

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
                sendErrorForRequest_(
                    frame.requestId,
                    FileSystemRpcStatus::STORAGE_ERROR
                );
                cancelFrameOperation_(frame);
                clearFrame_(frame);
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
        sendErrorForRequest_(
            pending.requestId,
            FileSystemRpcStatus::STORAGE_ERROR
        );
        cancelFrameOperation_(pending);
        clearFrame_(pending);
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

    if (!frame->uploadContinuation && jobs.deadlineExpired(token, nowMs)) {
        sendErrorForRequest_(frame->requestId, FileSystemRpcStatus::BUSY);
        cancelFrameOperation_(*frame);
        (void)jobs.expire(token, nowMs);
        clearFrame_(*frame);
        return;
    }

    // An operation already acknowledged as pending remains retained while
    // playback is active. It performs no I/O and resumes from the same durable
    // checkpoint after playback stops.
    if (playbackActive && frame->operation != PendingOperation::FRAME) return;

    const auto quota = playbackActive
        ? core::persistence::PRODUCT_PERSISTENCE_QUOTA_ENDPOINT_FRAME
        : quotaFor_(*frame, messageId);
    if (!jobs.prepareAdvance(token, quota) || !jobs.claimAdvance(token, nowMs)) {
        sendErrorForRequest_(frame->requestId, FileSystemRpcStatus::BUSY);
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
            if (frame->operation != PendingOperation::FRAME) {
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
        sendErrorForRequest_(frame->requestId, FileSystemRpcStatus::TOO_LARGE);
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

    if (response && response.value() > 0U) {
        transport_.send(response_, response.value());
    }
    clearFrame_(*frame);
}

FLASHMEM bool FileSystemRpcEndpoint::active() const {
    return active_;
}

FLASHMEM void FileSystemRpcEndpoint::handleReceive_(const uint8_t* data, size_t size) {
    if (!active_ || !data || size == 0U) return;
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
    auto admitted = files_.persistenceJobs().admit({
        .owner = core::persistence::ProductPersistenceJobOwner::FILESYSTEM_RPC,
        .nowMs = nowMs,
        .deadlineAfterMs = FILESYSTEM_RPC_TOTAL_WRITE_TIMEOUT_MS,
        .quota = core::persistence::PRODUCT_PERSISTENCE_QUOTA_ENDPOINT_FRAME,
    });
    if (!admitted) {
        clearFrame_(*frame);
        sendError_(data, size, FileSystemRpcStatus::BUSY);
        return;
    }
    frame->token = std::move(admitted.value());
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
}

FLASHMEM void FileSystemRpcEndpoint::advanceUploadTimeout_(uint32_t nowMs) {
    auto& jobs = files_.persistenceJobs();
    if (!upload_job_.valid() || !jobs.isActive(upload_job_)) return;
    if (!jobs.prepareAdvance(
            upload_job_,
            core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
        ) ||
        !jobs.claimAdvance(upload_job_, nowMs)) {
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
            frame.operation == PendingOperation::WRITE_COMMIT) {
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

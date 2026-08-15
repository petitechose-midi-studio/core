#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "persistence/ProductConditionalMutationPlan.hpp"

namespace core::protocol::filesystem {

using oc::type::Result;
using internal::ByteReader;
using internal::ByteWriter;
using internal::bufferTooSmall;
using internal::readPath;
using internal::writeFrameHeader;

namespace mutation = core::persistence::conditional_mutation;

namespace {

constexpr const char* RESOLVED_TMP_PREFIX = "/midi-studio/tmp/";
constexpr const char* RESOLVED_PROTOCOL_TMP_PREFIX = "/midi-studio/tmp/rpc-";

constexpr char asciiLower(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

FLASHMEM bool pathStartsWith(const char* path, const char* prefix) {
    if (!path || !prefix) return false;
    while (*prefix != '\0') {
        if (*path == '\0' || asciiLower(*path) != asciiLower(*prefix)) {
            return false;
        }
        ++path;
        ++prefix;
    }
    return true;
}

FLASHMEM bool pathEquals(const char* lhs, const char* rhs) {
    if (!lhs || !rhs) return false;
    while (*lhs != '\0' && *rhs != '\0') {
        if (asciiLower(*lhs) != asciiLower(*rhs)) return false;
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

FLASHMEM bool isReservedPath(const char* normalized) {
    return pathStartsWith(normalized, RESOLVED_PROTOCOL_TMP_PREFIX);
}

FLASHMEM bool isStagingPath(const char* normalized) {
    return pathStartsWith(normalized, RESOLVED_TMP_PREFIX) &&
           normalized[std::strlen(RESOLVED_TMP_PREFIX)] != '\0';
}

FLASHMEM bool containsFatShortNameAliasSyntax(const char* normalized) {
    return normalized != nullptr && std::strchr(normalized, '~') != nullptr;
}

FLASHMEM FileSystemRpcStatus rpcStatus(mutation::Status status) {
    switch (status) {
        case mutation::Status::OK:
            return FileSystemRpcStatus::OK;
        case mutation::Status::INVALID_ARGUMENT:
            return FileSystemRpcStatus::INVALID_ARGUMENT;
        case mutation::Status::NOT_FOUND:
            return FileSystemRpcStatus::NOT_FOUND;
        case mutation::Status::BUSY:
            return FileSystemRpcStatus::BUSY;
        case mutation::Status::TOO_LARGE:
            return FileSystemRpcStatus::TOO_LARGE;
        case mutation::Status::STORAGE_ERROR:
            return FileSystemRpcStatus::STORAGE_ERROR;
        case mutation::Status::INVALID_STATE:
            return FileSystemRpcStatus::INVALID_STATE;
        case mutation::Status::UNSUPPORTED:
            return FileSystemRpcStatus::UNSUPPORTED;
        case mutation::Status::PRECONDITION_FAILED:
            return FileSystemRpcStatus::PRECONDITION_FAILED;
        default:
            return FileSystemRpcStatus::INVALID_STATE;
    }
}

FLASHMEM FileSystemRpcMutationOutcome rpcOutcome(mutation::Outcome outcome) {
    switch (outcome) {
        case mutation::Outcome::APPLIED:
            return FileSystemRpcMutationOutcome::APPLIED;
        case mutation::Outcome::ALREADY_APPLIED:
            return FileSystemRpcMutationOutcome::ALREADY_APPLIED;
        case mutation::Outcome::NONE:
        default:
            return FileSystemRpcMutationOutcome::NONE;
    }
}

FLASHMEM FileSystemRpcMutationSubject rpcSubject(mutation::Subject subject) {
    switch (subject) {
        case mutation::Subject::SOURCE:
            return FileSystemRpcMutationSubject::SOURCE;
        case mutation::Subject::STAGING:
            return FileSystemRpcMutationSubject::STAGING;
        case mutation::Subject::NONE:
        default:
            return FileSystemRpcMutationSubject::NONE;
    }
}

FLASHMEM FileSystemRpcStatus normalizeMutationPathInPlace(
    core::persistence::ProductFileService& files,
    char* path,
    size_t pathSize
) {
    char raw[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    if (path == nullptr || pathSize == 0U || pathSize > sizeof(raw)) {
        return FileSystemRpcStatus::INVALID_ARGUMENT;
    }
    std::memcpy(raw, path, pathSize);
    auto normalized = files.resolvePath(raw, path, pathSize);
    return normalized ? FileSystemRpcStatus::OK : internal::mapError(normalized.error());
}

FLASHMEM Result<size_t> encodeConditionalResponse(
    FileSystemRpcMessageId messageId,
    uint16_t requestId,
    FileSystemRpcStatus status,
    FileSystemRpcMutationOutcome outcome,
    FileSystemRpcMutationSubject subject,
    uint32_t operationId,
    const uint8_t* observedSha256,
    uint8_t* response,
    size_t responseSize
) {
    static constexpr uint8_t zeroDigest[FILESYSTEM_RPC_SHA256_SIZE] PROGMEM = {};
    ByteWriter writer(response, responseSize);
    if (!writeFrameHeader(writer, messageId, requestId) ||
        !writer.writeU8(static_cast<uint8_t>(status)) ||
        !writer.writeU8(static_cast<uint8_t>(outcome)) ||
        !writer.writeU8(static_cast<uint8_t>(subject)) ||
        !writer.writeU32(operationId) ||
        !writer.writeBytes(
            observedSha256 ? observedSha256 : zeroDigest,
            FILESYSTEM_RPC_SHA256_SIZE
        )) {
        return bufferTooSmall();
    }
    return Result<size_t>::ok(writer.position());
}

FLASHMEM FileSystemRpcMessageId conditionalResponseId(
    FileSystemRpcMessageId requestId
) {
    return requestId == FileSystemRpcMessageId::CONDITIONAL_DELETE_REQUEST
        ? FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE
        : FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE;
}

FLASHMEM FileSystemRpcMessageId conditionalResponseId(mutation::Kind kind) {
    return kind == mutation::Kind::DELETE
        ? FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE
        : FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE;
}

FLASHMEM FileSystemRpcStatus prepareConditionalMutation(
    core::persistence::ProductFileService& files,
    const FileSystemRpcFrame& frame,
    mutation::Journal& journal
) {
    const uint8_t* expected = nullptr;
    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU32(journal.operationId) ||
        !reader.readBytes(expected, FILESYSTEM_RPC_SHA256_SIZE)) {
        return FileSystemRpcStatus::INVALID_ARGUMENT;
    }
    mutation::copyDigest(journal.expectedSourceSha256, expected);

    if (frame.messageId == FileSystemRpcMessageId::CONDITIONAL_REPLACE_REQUEST) {
        const uint8_t* replacement = nullptr;
        journal.kind = mutation::Kind::REPLACE;
        if (!reader.readBytes(replacement, FILESYSTEM_RPC_SHA256_SIZE) ||
            !readPath(reader, journal.currentPath, sizeof(journal.currentPath)) ||
            !readPath(reader, journal.stagingPath, sizeof(journal.stagingPath)) ||
            reader.remaining() != 0U) {
            return FileSystemRpcStatus::INVALID_ARGUMENT;
        }
        mutation::copyDigest(journal.replacementSha256, replacement);

        const auto currentStatus = normalizeMutationPathInPlace(
            files, journal.currentPath, sizeof(journal.currentPath)
        );
        const auto stagingStatus = normalizeMutationPathInPlace(
            files, journal.stagingPath, sizeof(journal.stagingPath)
        );
        if (currentStatus != FileSystemRpcStatus::OK ||
            stagingStatus != FileSystemRpcStatus::OK ||
            pathEquals(journal.currentPath, journal.stagingPath) ||
            isReservedPath(journal.currentPath) ||
            isReservedPath(journal.stagingPath) ||
            containsFatShortNameAliasSyntax(journal.currentPath) ||
            containsFatShortNameAliasSyntax(journal.stagingPath) ||
            !isStagingPath(journal.stagingPath)) {
            return FileSystemRpcStatus::INVALID_ARGUMENT;
        }
        return FileSystemRpcStatus::OK;
    }

    if (frame.messageId != FileSystemRpcMessageId::CONDITIONAL_DELETE_REQUEST) {
        return FileSystemRpcStatus::INVALID_ARGUMENT;
    }
    journal.kind = mutation::Kind::DELETE;
    if (!readPath(reader, journal.currentPath, sizeof(journal.currentPath)) ||
        reader.remaining() != 0U) {
        return FileSystemRpcStatus::INVALID_ARGUMENT;
    }
    const auto currentStatus = normalizeMutationPathInPlace(
        files, journal.currentPath, sizeof(journal.currentPath)
    );
    if (currentStatus != FileSystemRpcStatus::OK ||
        isReservedPath(journal.currentPath) ||
        containsFatShortNameAliasSyntax(journal.currentPath)) {
        return FileSystemRpcStatus::INVALID_ARGUMENT;
    }
    return FileSystemRpcStatus::OK;
}

}  // namespace

FLASHMEM bool internal::isProtocolReservedPath(
    core::persistence::ProductFileService& files,
    const char* productPath
) {
    char normalized[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    auto resolved = files.resolvePath(productPath, normalized, sizeof(normalized));
    return resolved && isReservedPath(normalized);
}

FLASHMEM FileSystemRpcStatus FileSystemRpcHandler::recoverConditionalMutation_() {
    if (hasActiveWriteSession()) {
        return FileSystemRpcStatus::BUSY;
    }

    const auto state = files_.storageState();
    if (state != core::persistence::ProductStorageState::READY) {
        switch (state) {
            case core::persistence::ProductStorageState::ABSENT:
                return FileSystemRpcStatus::STORAGE_ERROR;
            case core::persistence::ProductStorageState::EXHAUSTED:
                return FileSystemRpcStatus::TOO_LARGE;
            default:
                return FileSystemRpcStatus::BUSY;
        }
    }

    auto acquired = files_.acquireMutation(
        core::persistence::ProductMutationOwner::FILESYSTEM_RPC
    );
    if (!acquired) return internal::mapError(acquired.error());
    auto lease = std::move(acquired.value());

    bool quarantined = false;
    auto status = mutation::recoverPendingMutation(files_, lease, quarantined);
    if (quarantined) {
        conditionalRecoveryState_ =
            FileSystemRpcConditionalRecoveryState::CORRUPT_JOURNAL_QUARANTINED;
    }

    auto released = files_.releaseMutation(lease);
    if (!released && status == mutation::Status::OK) {
        status = mutation::statusFromError(released.error());
    }
    if (status != mutation::Status::OK &&
        files_.storageState() == core::persistence::ProductStorageState::READY) {
        (void)files_.requireRecovery(mutation::recoveryError(status));
    }
    return rpcStatus(status);
}

bool FileSystemRpcHandler::conditionalRecoveryDue_(uint32_t nowMs) const {
    if (conditionalRecoveryState_ == FileSystemRpcConditionalRecoveryState::NOT_CHECKED) {
        return true;
    }
    if (conditionalRecoveryIdentity_ != files_.storageIdentity()) {
        return true;
    }
    return conditionalRecoveryState_ == FileSystemRpcConditionalRecoveryState::BLOCKED &&
           static_cast<int32_t>(nowMs - conditionalRecoveryRetryAtMs_) >= 0;
}

FLASHMEM void FileSystemRpcHandler::updateConditionalRecovery_(uint32_t nowMs) {
    if (!conditionalRecoveryDue_(nowMs)) return;

    const auto status = recoverConditionalMutation_();
    conditionalRecoveryStatus_ = status;
    conditionalRecoveryIdentity_ = files_.storageIdentity();
    if (status == FileSystemRpcStatus::OK) {
        if (conditionalRecoveryState_ !=
            FileSystemRpcConditionalRecoveryState::CORRUPT_JOURNAL_QUARANTINED) {
            conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::READY;
        }
        conditionalRecoveryRetryAtMs_ = 0;
        return;
    }

    conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::BLOCKED;
    conditionalRecoveryRetryAtMs_ = nowMs + CONDITIONAL_RECOVERY_RETRY_MS;
}

FLASHMEM Result<size_t> FileSystemRpcHandler::beginCooperativeConditionalMutation_(
    const FileSystemRpcFrame& frame,
    mutation::ConditionalMutationPlan& plan,
    uint8_t* response,
    size_t responseSize
) {
    mutation::Journal journal{};
    const auto responseId = conditionalResponseId(frame.messageId);
    const auto prepared = prepareConditionalMutation(files_, frame, journal);
    if (prepared != FileSystemRpcStatus::OK) {
        return encodeConditionalResponse(
            responseId,
            frame.requestId,
            prepared,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            journal.operationId,
            nullptr,
            response,
            responseSize
        );
    }

    auto acquired = files_.acquireMutation(
        core::persistence::ProductMutationOwner::FILESYSTEM_RPC
    );
    if (!acquired) {
        return encodeConditionalResponse(
            responseId,
            frame.requestId,
            internal::mapError(acquired.error()),
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            journal.operationId,
            nullptr,
            response,
            responseSize
        );
    }
    auto lease = std::move(acquired.value());
    auto begun = plan.begin(files_, std::move(lease), journal);
    if (!begun) {
        if (lease.valid() && files_.owns(lease)) {
            (void)files_.releaseMutation(lease);
        }
        return encodeConditionalResponse(
            responseId,
            frame.requestId,
            internal::mapError(begun.error()),
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            journal.operationId,
            nullptr,
            response,
            responseSize
        );
    }
    return Result<size_t>::ok(0U);
}

FLASHMEM Result<size_t> FileSystemRpcHandler::advanceCooperativeConditionalMutation_(
    mutation::ConditionalMutationPlan& plan,
    uint16_t requestId,
    uint32_t nowMs,
    uint8_t* response,
    size_t responseSize
) {
    if (plan.active() && !plan.advance(files_, response, responseSize)) {
        return Result<size_t>::ok(0U);
    }

    if (!plan.terminal()) {
        return encodeConditionalResponse(
            conditionalResponseId(plan.kind()),
            requestId,
            FileSystemRpcStatus::INVALID_STATE,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            plan.operationId(),
            nullptr,
            response,
            responseSize
        );
    }

    conditionalRecoveryIdentity_ = files_.storageIdentity();
    if (plan.recoveryRequired()) {
        conditionalRecoveryStatus_ = rpcStatus(plan.status());
        conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::BLOCKED;
        conditionalRecoveryRetryAtMs_ = nowMs + CONDITIONAL_RECOVERY_RETRY_MS;
    } else {
        conditionalRecoveryStatus_ = FileSystemRpcStatus::OK;
        conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::READY;
        conditionalRecoveryRetryAtMs_ = 0U;
    }

    return encodeConditionalResponse(
        conditionalResponseId(plan.kind()),
        requestId,
        rpcStatus(plan.status()),
        rpcOutcome(plan.outcome()),
        rpcSubject(plan.subject()),
        plan.operationId(),
        plan.observedDigest(),
        response,
        responseSize
    );
}

FLASHMEM void FileSystemRpcHandler::cancelCooperativeConditionalMutation_(
    mutation::ConditionalMutationPlan& plan
) {
    plan.cancel(files_);
    if (!plan.recoveryRequired()) return;

    conditionalRecoveryStatus_ = rpcStatus(plan.status());
    conditionalRecoveryIdentity_ = files_.storageIdentity();
    conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::BLOCKED;
    conditionalRecoveryRetryAtMs_ = 0U;
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleConditionalReplace_(
    const FileSystemRpcFrame& frame,
    uint32_t nowMs,
    uint8_t* response,
    size_t responseSize
) {
    mutation::ConditionalMutationPlan plan{};
    auto result = beginCooperativeConditionalMutation_(
        frame, plan, response, responseSize
    );
    while (result && result.value() == 0U && plan.active()) {
        result = advanceCooperativeConditionalMutation_(
            plan, frame.requestId, nowMs, response, responseSize
        );
    }
    return result;
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleConditionalDelete_(
    const FileSystemRpcFrame& frame,
    uint32_t nowMs,
    uint8_t* response,
    size_t responseSize
) {
    mutation::ConditionalMutationPlan plan{};
    auto result = beginCooperativeConditionalMutation_(
        frame, plan, response, responseSize
    );
    while (result && result.value() == 0U && plan.active()) {
        result = advanceCooperativeConditionalMutation_(
            plan, frame.requestId, nowMs, response, responseSize
        );
    }
    return result;
}

}  // namespace core::protocol::filesystem

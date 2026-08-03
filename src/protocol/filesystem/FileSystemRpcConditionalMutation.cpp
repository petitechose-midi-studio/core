#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "protocol/filesystem/FileSystemRpcConditionalTransaction.hpp"
#include "protocol/filesystem/FileSystemRpcDigest.hpp"

namespace core::protocol::filesystem {

using oc::type::ErrorCode;
using oc::type::Result;
using internal::ByteReader;
using internal::ByteWriter;
using internal::bufferTooSmall;
using internal::readPath;
using internal::writeFrameHeader;

namespace mutation = conditional_mutation;

namespace {

class ScopedRpcMutationLease {
public:
    ScopedRpcMutationLease(
        core::persistence::ProductFileService& files,
        core::persistence::ProductMutationLease&& lease
    ) : files_(files), lease_(std::move(lease)) {}

    ~ScopedRpcMutationLease() {
        if (lease_.valid()) (void)files_.releaseMutation(lease_);
    }

    const core::persistence::ProductMutationLease& lease() const { return lease_; }

    oc::type::Result<void> release() {
        if (!lease_.valid()) return oc::type::Result<void>::ok();
        return files_.releaseMutation(lease_);
    }

private:
    core::persistence::ProductFileService& files_;
    core::persistence::ProductMutationLease lease_{};
};

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
    return mutation::normalizeMutationPath(files, raw, path, pathSize);
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

}  // namespace

FLASHMEM bool internal::isProtocolReservedPath(
    core::persistence::ProductFileService& files,
    const char* productPath
) {
    char normalized[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    auto resolved = files.resolvePath(productPath, normalized, sizeof(normalized));
    return resolved && mutation::isReservedPath(normalized);
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
    auto status = mutation::recoverPendingMutation(
        files_,
        lease,
        quarantined
    );
    if (quarantined) {
        conditionalRecoveryState_ =
            FileSystemRpcConditionalRecoveryState::CORRUPT_JOURNAL_QUARANTINED;
    }

    auto released = files_.releaseMutation(lease);
    if (!released && status == FileSystemRpcStatus::OK) {
        status = internal::mapError(released.error());
    }
    if (status != FileSystemRpcStatus::OK &&
        files_.storageState() == core::persistence::ProductStorageState::READY) {
        (void)files_.requireRecovery(mutation::recoveryError(status));
    }
    return status;
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

FLASHMEM Result<size_t> FileSystemRpcHandler::handleConditionalReplace_(
    const FileSystemRpcFrame& frame,
    uint32_t nowMs,
    uint8_t* response,
    size_t responseSize
) {
    uint32_t operationId = 0;
    const uint8_t* expected = nullptr;
    const uint8_t* replacement = nullptr;
    mutation::Journal journal{};
    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU32(operationId) ||
        !reader.readBytes(expected, FILESYSTEM_RPC_SHA256_SIZE) ||
        !reader.readBytes(replacement, FILESYSTEM_RPC_SHA256_SIZE) ||
        !readPath(reader, journal.currentPath, sizeof(journal.currentPath)) ||
        !readPath(reader, journal.stagingPath, sizeof(journal.stagingPath)) ||
        reader.remaining() != 0) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    journal.kind = mutation::Kind::REPLACE;
    journal.operationId = operationId;
    mutation::copyDigest(journal.expectedSourceSha256, expected);
    mutation::copyDigest(journal.replacementSha256, replacement);
    const auto currentPathStatus = normalizeMutationPathInPlace(
        files_, journal.currentPath, sizeof(journal.currentPath)
    );
    const auto stagingPathStatus = normalizeMutationPathInPlace(
        files_, journal.stagingPath, sizeof(journal.stagingPath)
    );
    if (currentPathStatus != FileSystemRpcStatus::OK ||
        stagingPathStatus != FileSystemRpcStatus::OK ||
        // FAT is case-insensitive: differently cased spellings may still name
        // the same physical file. Never let idempotent cleanup unlink the
        // canonical source through such an alias.
        mutation::pathEquals(journal.currentPath, journal.stagingPath) ||
        mutation::isReservedPath(journal.currentPath) ||
        mutation::isReservedPath(journal.stagingPath) ||
        mutation::containsFatShortNameAliasSyntax(journal.currentPath) ||
        mutation::containsFatShortNameAliasSyntax(journal.stagingPath) ||
        !mutation::isStagingPath(journal.stagingPath)) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
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
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            internal::mapError(acquired.error()),
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    ScopedRpcMutationLease transaction(files_, std::move(acquired.value()));
    const auto& lease = transaction.lease();

    auto current = mutation::readDigest(files_, lease, journal.currentPath);
    if (current.status == FileSystemRpcStatus::OK &&
        mutation::digestEquals(current.sha256, journal.replacementSha256)) {
        auto backup = files_.stat(lease, mutation::BACKUP_PATH);
        const bool unexpectedBackup =
            backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND;
        const auto stagingCleanup =
            mutation::removeIfExists(files_, lease, journal.stagingPath);
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            unexpectedBackup
                ? FileSystemRpcStatus::INVALID_STATE
                : stagingCleanup,
            !unexpectedBackup && stagingCleanup == FileSystemRpcStatus::OK
                ? FileSystemRpcMutationOutcome::ALREADY_APPLIED
                : FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    if (current.status != FileSystemRpcStatus::OK ||
        !mutation::digestEquals(current.sha256, journal.expectedSourceSha256)) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            current.status == FileSystemRpcStatus::OK
                ? FileSystemRpcStatus::PRECONDITION_FAILED
                : current.status,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::SOURCE,
            operationId,
            current.status == FileSystemRpcStatus::OK ? current.sha256 : nullptr,
            response,
            responseSize
        );
    }
    auto staging = mutation::readDigest(files_, lease, journal.stagingPath);
    if (staging.status != FileSystemRpcStatus::OK ||
        !mutation::digestEquals(staging.sha256, journal.replacementSha256)) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            staging.status == FileSystemRpcStatus::OK
                ? FileSystemRpcStatus::PRECONDITION_FAILED
                : staging.status,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::STAGING,
            operationId,
            staging.status == FileSystemRpcStatus::OK ? staging.sha256 : nullptr,
            response,
            responseSize
        );
    }

    auto backup = files_.stat(lease, mutation::BACKUP_PATH);
    if (backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_STATE,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }

    auto journalStatus = mutation::writeJournal(files_, lease, journal);
    FileSystemRpcMutationOutcome outcome = FileSystemRpcMutationOutcome::NONE;
    if (journalStatus == FileSystemRpcStatus::OK) {
        const auto executed = mutation::executeJournal(files_, lease, journal);
        journalStatus = executed.status;
        if (executed.status == FileSystemRpcStatus::OK) {
            outcome = FileSystemRpcMutationOutcome::APPLIED;
        }
    }
    auto released = transaction.release();
    if (journalStatus == FileSystemRpcStatus::OK && !released) {
        journalStatus = internal::mapError(released.error());
        outcome = FileSystemRpcMutationOutcome::NONE;
    }
    conditionalRecoveryStatus_ = journalStatus;
    conditionalRecoveryIdentity_ = files_.storageIdentity();
    if (journalStatus == FileSystemRpcStatus::OK) {
        conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::READY;
        conditionalRecoveryRetryAtMs_ = 0;
    } else {
        if (files_.storageState() == core::persistence::ProductStorageState::READY) {
            (void)files_.requireRecovery(mutation::recoveryError(journalStatus));
        }
        conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::BLOCKED;
        conditionalRecoveryRetryAtMs_ = nowMs + CONDITIONAL_RECOVERY_RETRY_MS;
        conditionalRecoveryIdentity_ = files_.storageIdentity();
    }
    return encodeConditionalResponse(
        FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
        frame.requestId,
        journalStatus,
        outcome,
        FileSystemRpcMutationSubject::NONE,
        operationId,
        nullptr,
        response,
        responseSize
    );
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleConditionalDelete_(
    const FileSystemRpcFrame& frame,
    uint32_t nowMs,
    uint8_t* response,
    size_t responseSize
) {
    uint32_t operationId = 0;
    const uint8_t* expected = nullptr;
    mutation::Journal journal{};
    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU32(operationId) ||
        !reader.readBytes(expected, FILESYSTEM_RPC_SHA256_SIZE) ||
        !readPath(reader, journal.currentPath, sizeof(journal.currentPath)) ||
        reader.remaining() != 0) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    journal.kind = mutation::Kind::DELETE;
    journal.operationId = operationId;
    mutation::copyDigest(journal.expectedSourceSha256, expected);
    const auto currentPathStatus = normalizeMutationPathInPlace(
        files_, journal.currentPath, sizeof(journal.currentPath)
    );
    if (currentPathStatus != FileSystemRpcStatus::OK ||
        mutation::isReservedPath(journal.currentPath) ||
        mutation::containsFatShortNameAliasSyntax(journal.currentPath)) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
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
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            internal::mapError(acquired.error()),
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    ScopedRpcMutationLease transaction(files_, std::move(acquired.value()));
    const auto& lease = transaction.lease();

    auto current = mutation::readDigest(files_, lease, journal.currentPath);
    if (current.status == FileSystemRpcStatus::NOT_FOUND) {
        auto backup = files_.stat(lease, mutation::BACKUP_PATH);
        const bool unexpectedBackup =
            backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND;
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            unexpectedBackup
                ? FileSystemRpcStatus::INVALID_STATE
                : FileSystemRpcStatus::OK,
            unexpectedBackup
                ? FileSystemRpcMutationOutcome::NONE
                : FileSystemRpcMutationOutcome::ALREADY_APPLIED,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    if (current.status != FileSystemRpcStatus::OK ||
        !mutation::digestEquals(current.sha256, journal.expectedSourceSha256)) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            current.status == FileSystemRpcStatus::OK
                ? FileSystemRpcStatus::PRECONDITION_FAILED
                : current.status,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::SOURCE,
            operationId,
            current.status == FileSystemRpcStatus::OK ? current.sha256 : nullptr,
            response,
            responseSize
        );
    }

    auto backup = files_.stat(lease, mutation::BACKUP_PATH);
    if (backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_STATE,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }

    auto journalStatus = mutation::writeJournal(files_, lease, journal);
    FileSystemRpcMutationOutcome outcome = FileSystemRpcMutationOutcome::NONE;
    if (journalStatus == FileSystemRpcStatus::OK) {
        const auto executed = mutation::executeJournal(files_, lease, journal);
        journalStatus = executed.status;
        if (executed.status == FileSystemRpcStatus::OK) {
            outcome = FileSystemRpcMutationOutcome::APPLIED;
        }
    }
    auto released = transaction.release();
    if (journalStatus == FileSystemRpcStatus::OK && !released) {
        journalStatus = internal::mapError(released.error());
        outcome = FileSystemRpcMutationOutcome::NONE;
    }
    conditionalRecoveryStatus_ = journalStatus;
    conditionalRecoveryIdentity_ = files_.storageIdentity();
    if (journalStatus == FileSystemRpcStatus::OK) {
        conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::READY;
        conditionalRecoveryRetryAtMs_ = 0;
    } else {
        if (files_.storageState() == core::persistence::ProductStorageState::READY) {
            (void)files_.requireRecovery(mutation::recoveryError(journalStatus));
        }
        conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::BLOCKED;
        conditionalRecoveryRetryAtMs_ = nowMs + CONDITIONAL_RECOVERY_RETRY_MS;
        conditionalRecoveryIdentity_ = files_.storageIdentity();
    }
    return encodeConditionalResponse(
        FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
        frame.requestId,
        journalStatus,
        outcome,
        FileSystemRpcMutationSubject::NONE,
        operationId,
        nullptr,
        response,
        responseSize
    );
}

}  // namespace core::protocol::filesystem

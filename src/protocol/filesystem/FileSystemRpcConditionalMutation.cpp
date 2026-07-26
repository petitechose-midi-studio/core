#include "protocol/filesystem/FileSystemRpcInternal.hpp"

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

FLASHMEM bool internal::isConditionalMutationReservedPath(
    core::persistence::ProductFileService& files,
    const char* productPath
) {
    char normalized[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    auto resolved = files.resolvePath(productPath, normalized, sizeof(normalized));
    return resolved && mutation::isReservedPath(normalized);
}

FLASHMEM FileSystemRpcStatus FileSystemRpcHandler::recoverConditionalMutation_() {
    if (writeSession_.active || files_.writeSessionActive()) {
        return FileSystemRpcStatus::BUSY;
    }
    mutation::Journal journal{};
    bool present = false;
    bool corrupt = false;
    const auto loaded = mutation::readJournal(files_, journal, present, corrupt);
    if (loaded != FileSystemRpcStatus::OK) {
        if (!corrupt) return loaded;
        const auto quarantined = mutation::quarantineCorruptJournal(files_);
        if (quarantined != FileSystemRpcStatus::OK) return quarantined;
        conditionalRecoveryState_ =
            FileSystemRpcConditionalRecoveryState::CORRUPT_JOURNAL_QUARANTINED;
        // This is an orphaned journal stream, not a user staging asset.
        return mutation::removeIfExists(files_, mutation::JOURNAL_STAGING_PATH);
    }
    const auto stagingCleanup =
        mutation::removeIfExists(files_, mutation::JOURNAL_STAGING_PATH);
    if (stagingCleanup != FileSystemRpcStatus::OK) return stagingCleanup;
    if (!present) return FileSystemRpcStatus::OK;
    const auto executed = mutation::executeJournal(files_, journal);
    return executed.status;
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleConditionalReplace_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    uint32_t operationId = 0;
    const uint8_t* expected = nullptr;
    const uint8_t* replacement = nullptr;
    char currentRaw[PATH_BUFFER_SIZE] = {};
    char stagingRaw[PATH_BUFFER_SIZE] = {};
    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU32(operationId) ||
        !reader.readBytes(expected, FILESYSTEM_RPC_SHA256_SIZE) ||
        !reader.readBytes(replacement, FILESYSTEM_RPC_SHA256_SIZE) ||
        !readPath(reader, currentRaw, sizeof(currentRaw)) ||
        !readPath(reader, stagingRaw, sizeof(stagingRaw)) ||
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
    if (writeSession_.active || files_.writeSessionActive()) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::BUSY,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }

    mutation::Journal journal{};
    journal.kind = mutation::Kind::REPLACE;
    journal.operationId = operationId;
    mutation::copyDigest(journal.expectedSourceSha256, expected);
    mutation::copyDigest(journal.replacementSha256, replacement);
    const auto currentPathStatus = mutation::normalizeMutationPath(
        files_, currentRaw, journal.currentPath, sizeof(journal.currentPath)
    );
    const auto stagingPathStatus = mutation::normalizeMutationPath(
        files_, stagingRaw, journal.stagingPath, sizeof(journal.stagingPath)
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

    auto current = mutation::readDigest(files_, journal.currentPath);
    if (current.status == FileSystemRpcStatus::OK &&
        mutation::digestEquals(current.sha256, journal.replacementSha256)) {
        auto backup = files_.stat(mutation::BACKUP_PATH);
        const bool unexpectedBackup =
            backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND;
        const auto stagingCleanup =
            mutation::removeIfExists(files_, journal.stagingPath);
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
    auto staging = mutation::readDigest(files_, journal.stagingPath);
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

    auto backup = files_.stat(mutation::BACKUP_PATH);
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

    conditionalRecoveryChecked_ = false;
    conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::NOT_CHECKED;
    conditionalRecoveryStatus_ = FileSystemRpcStatus::OK;
    auto journalStatus = mutation::writeJournal(files_, journal);
    if (journalStatus == FileSystemRpcStatus::OK) {
        const auto executed = mutation::executeJournal(files_, journal);
        journalStatus = executed.status;
        if (executed.status == FileSystemRpcStatus::OK) {
            conditionalRecoveryChecked_ = true;
            conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::READY;
        }
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
            frame.requestId,
            executed.status,
            executed.status == FileSystemRpcStatus::OK
                ? FileSystemRpcMutationOutcome::APPLIED
                : FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    return encodeConditionalResponse(
        FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE,
        frame.requestId,
        journalStatus,
        FileSystemRpcMutationOutcome::NONE,
        FileSystemRpcMutationSubject::NONE,
        operationId,
        nullptr,
        response,
        responseSize
    );
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleConditionalDelete_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    uint32_t operationId = 0;
    const uint8_t* expected = nullptr;
    char currentRaw[PATH_BUFFER_SIZE] = {};
    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU32(operationId) ||
        !reader.readBytes(expected, FILESYSTEM_RPC_SHA256_SIZE) ||
        !readPath(reader, currentRaw, sizeof(currentRaw)) ||
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
    if (writeSession_.active || files_.writeSessionActive()) {
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::BUSY,
            FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }

    mutation::Journal journal{};
    journal.kind = mutation::Kind::DELETE;
    journal.operationId = operationId;
    mutation::copyDigest(journal.expectedSourceSha256, expected);
    const auto currentPathStatus = mutation::normalizeMutationPath(
        files_, currentRaw, journal.currentPath, sizeof(journal.currentPath)
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

    auto current = mutation::readDigest(files_, journal.currentPath);
    if (current.status == FileSystemRpcStatus::NOT_FOUND) {
        auto backup = files_.stat(mutation::BACKUP_PATH);
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

    auto backup = files_.stat(mutation::BACKUP_PATH);
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

    conditionalRecoveryChecked_ = false;
    conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::NOT_CHECKED;
    conditionalRecoveryStatus_ = FileSystemRpcStatus::OK;
    auto journalStatus = mutation::writeJournal(files_, journal);
    if (journalStatus == FileSystemRpcStatus::OK) {
        const auto executed = mutation::executeJournal(files_, journal);
        journalStatus = executed.status;
        if (executed.status == FileSystemRpcStatus::OK) {
            conditionalRecoveryChecked_ = true;
            conditionalRecoveryState_ = FileSystemRpcConditionalRecoveryState::READY;
        }
        return encodeConditionalResponse(
            FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
            frame.requestId,
            executed.status,
            executed.status == FileSystemRpcStatus::OK
                ? FileSystemRpcMutationOutcome::APPLIED
                : FileSystemRpcMutationOutcome::NONE,
            FileSystemRpcMutationSubject::NONE,
            operationId,
            nullptr,
            response,
            responseSize
        );
    }
    return encodeConditionalResponse(
        FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE,
        frame.requestId,
        journalStatus,
        FileSystemRpcMutationOutcome::NONE,
        FileSystemRpcMutationSubject::NONE,
        operationId,
        nullptr,
        response,
        responseSize
    );
}

}  // namespace core::protocol::filesystem

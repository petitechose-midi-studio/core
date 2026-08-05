#include "protocol/filesystem/FileSystemRpcConditionalTransaction.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/PersistenceChecksum.hpp"
#include "protocol/filesystem/FileSystemRpcDigest.hpp"
#include "protocol/filesystem/FileSystemRpcInternal.hpp"

namespace core::protocol::filesystem::conditional_mutation {

using oc::type::ErrorCode;
using internal::ByteReader;
using internal::ByteWriter;
using internal::mapError;

namespace {

constexpr const char* JOURNAL_QUARANTINE_PATH =
    FILESYSTEM_RPC_CONDITIONAL_JOURNAL_QUARANTINE_PATH;
constexpr const char* RESOLVED_TMP_PREFIX = "/midi-studio/tmp/";
constexpr const char* RESOLVED_PROTOCOL_TMP_PREFIX = "/midi-studio/tmp/rpc-";
constexpr uint8_t JOURNAL_VERSION = 1;
constexpr uint8_t JOURNAL_MAGIC[] PROGMEM = {'F', 'S', 'T', 'X'};
constexpr size_t JOURNAL_BUFFER_SIZE =
    sizeof(JOURNAL_MAGIC) +
    sizeof(uint8_t) +  // version
    sizeof(uint8_t) +  // kind
    sizeof(uint32_t) +  // operation id
    (2U * FILESYSTEM_RPC_SHA256_SIZE) +
    (2U * (sizeof(uint8_t) + oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) +
    sizeof(uint32_t);  // checksum
static_assert(JOURNAL_BUFFER_SIZE == 464U);

constexpr char asciiLower(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

FLASHMEM bool pathStartsWith(const char* path, const char* prefix) {
    if (!path || !prefix) return false;
    while (*prefix != '\0') {
        if (*path == '\0' || asciiLower(*path) != asciiLower(*prefix)) return false;
        ++path;
        ++prefix;
    }
    return true;
}

FLASHMEM bool isNotFound(FileSystemRpcStatus status) {
    return status == FileSystemRpcStatus::NOT_FOUND;
}

FLASHMEM FileSystemRpcStatus removeJournalLast(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease
) {
    const auto stagingCleanup = removeIfExists(files, lease, JOURNAL_STAGING_PATH);
    if (stagingCleanup != FileSystemRpcStatus::OK) return stagingCleanup;
    return removeIfExists(files, lease, JOURNAL_PATH);
}

FLASHMEM ExecutionResult executeReplace(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const Journal& journal
) {
    auto current = readDigest(files, lease, journal.currentPath);
    if (current.status == FileSystemRpcStatus::OK &&
        digestEquals(current.sha256, journal.replacementSha256)) {
        if (removeIfExists(files, lease, journal.stagingPath) != FileSystemRpcStatus::OK ||
            removeIfExists(files, lease, BACKUP_PATH) != FileSystemRpcStatus::OK ||
            removeJournalLast(files, lease) != FileSystemRpcStatus::OK) {
            return {FileSystemRpcStatus::STORAGE_ERROR, true};
        }
        return {FileSystemRpcStatus::OK, true};
    }

    if (current.status == FileSystemRpcStatus::OK) {
        if (!digestEquals(current.sha256, journal.expectedSourceSha256)) {
            return {FileSystemRpcStatus::PRECONDITION_FAILED, false};
        }
        auto staging = readDigest(files, lease, journal.stagingPath);
        if (staging.status != FileSystemRpcStatus::OK ||
            !digestEquals(staging.sha256, journal.replacementSha256)) {
            return {
                staging.status == FileSystemRpcStatus::OK
                    ? FileSystemRpcStatus::PRECONDITION_FAILED
                    : staging.status,
                false,
            };
        }
        auto stagingInfo = files.stat(lease, journal.stagingPath);
        if (!stagingInfo || stagingInfo.value().type != oc::interface::FileType::FILE) {
            return {
                stagingInfo ? FileSystemRpcStatus::INVALID_STATE
                            : mapError(stagingInfo.error()),
                false,
            };
        }
        auto promoted = core::persistence::commitProductFileTemp(
            files,
            lease,
            journal.currentPath,
            BACKUP_PATH,
            journal.stagingPath,
            stagingInfo.value().sizeBytes,
            staging.crc32
        );
        if (!promoted) return {mapError(promoted.error()), false};

        auto committed = readDigest(files, lease, journal.currentPath);
        if (committed.status != FileSystemRpcStatus::OK ||
            !digestEquals(committed.sha256, journal.replacementSha256)) {
            return {FileSystemRpcStatus::STORAGE_ERROR, false};
        }
        if (removeJournalLast(files, lease) != FileSystemRpcStatus::OK) {
            return {FileSystemRpcStatus::STORAGE_ERROR, true};
        }
        return {FileSystemRpcStatus::OK, true};
    }

    if (current.status != FileSystemRpcStatus::NOT_FOUND) {
        return {current.status, false};
    }

    auto backup = readDigest(files, lease, BACKUP_PATH);
    if (backup.status == FileSystemRpcStatus::NOT_FOUND) {
        return {FileSystemRpcStatus::STORAGE_ERROR, false};
    }
    if (backup.status != FileSystemRpcStatus::OK ||
        !digestEquals(backup.sha256, journal.expectedSourceSha256)) {
        return {
            backup.status == FileSystemRpcStatus::OK
                ? FileSystemRpcStatus::PRECONDITION_FAILED
                : backup.status,
            false,
        };
    }

    auto staging = readDigest(files, lease, journal.stagingPath);
    if (staging.status == FileSystemRpcStatus::OK &&
        digestEquals(staging.sha256, journal.replacementSha256)) {
        auto stagingInfo = files.stat(lease, journal.stagingPath);
        if (!stagingInfo || stagingInfo.value().type != oc::interface::FileType::FILE) {
            return {
                stagingInfo ? FileSystemRpcStatus::INVALID_STATE
                            : mapError(stagingInfo.error()),
                false,
            };
        }
        auto promoted = core::persistence::commitProductFileTemp(
            files,
            lease,
            journal.currentPath,
            BACKUP_PATH,
            journal.stagingPath,
            stagingInfo.value().sizeBytes,
            staging.crc32
        );
        if (!promoted) return {mapError(promoted.error()), false};
        if (removeJournalLast(files, lease) != FileSystemRpcStatus::OK) {
            return {FileSystemRpcStatus::STORAGE_ERROR, true};
        }
        return {FileSystemRpcStatus::OK, true};
    }

    auto restored = files.rename(lease, BACKUP_PATH, journal.currentPath);
    if (!restored) return {mapError(restored.error()), false};
    if (removeJournalLast(files, lease) != FileSystemRpcStatus::OK) {
        return {FileSystemRpcStatus::STORAGE_ERROR, false};
    }
    return {FileSystemRpcStatus::STORAGE_ERROR, false};
}

FLASHMEM ExecutionResult executeDelete(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const Journal& journal
) {
    auto current = readDigest(files, lease, journal.currentPath);
    if (current.status == FileSystemRpcStatus::OK) {
        if (!digestEquals(current.sha256, journal.expectedSourceSha256)) {
            return {FileSystemRpcStatus::PRECONDITION_FAILED, false};
        }
        auto backup = files.stat(lease, BACKUP_PATH);
        if (backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
            return {FileSystemRpcStatus::INVALID_STATE, false};
        }
        auto moved = files.rename(lease, journal.currentPath, BACKUP_PATH);
        if (!moved) return {mapError(moved.error()), false};
    } else if (current.status != FileSystemRpcStatus::NOT_FOUND) {
        return {current.status, false};
    }

    auto backup = readDigest(files, lease, BACKUP_PATH);
    if (backup.status == FileSystemRpcStatus::OK) {
        if (!digestEquals(backup.sha256, journal.expectedSourceSha256)) {
            return {FileSystemRpcStatus::PRECONDITION_FAILED, false};
        }
        auto removed = files.remove(lease, BACKUP_PATH);
        if (!removed) return {mapError(removed.error()), false};
    } else if (backup.status != FileSystemRpcStatus::NOT_FOUND) {
        return {backup.status, false};
    }

    if (removeJournalLast(files, lease) != FileSystemRpcStatus::OK) {
        return {FileSystemRpcStatus::STORAGE_ERROR, true};
    }
    return {FileSystemRpcStatus::OK, true};
}

}  // namespace

FLASHMEM ErrorCode recoveryError(FileSystemRpcStatus status) {
    switch (status) {
        case FileSystemRpcStatus::BUSY:
            return ErrorCode::HARDWARE_BUSY;
        case FileSystemRpcStatus::TOO_LARGE:
            return ErrorCode::RESOURCE_EXHAUSTED;
        case FileSystemRpcStatus::NOT_FOUND:
        case FileSystemRpcStatus::STORAGE_ERROR:
            return ErrorCode::STORAGE_WRITE_FAILED;
        case FileSystemRpcStatus::UNSUPPORTED:
        case FileSystemRpcStatus::PRECONDITION_FAILED:
        case FileSystemRpcStatus::INVALID_ARGUMENT:
        case FileSystemRpcStatus::INVALID_MESSAGE:
        case FileSystemRpcStatus::INVALID_STATE:
            return ErrorCode::INVALID_STATE;
        case FileSystemRpcStatus::OK:
        default:
            return ErrorCode::OK;
    }
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
    // Reserve the complete protocol-owned rpc-* namespace. The prefix is
    // compared with FAT case semantics and also catches RPC-~n short aliases
    // generated for ordinary/conditional journals, staging and backups.
    return pathStartsWith(normalized, RESOLVED_PROTOCOL_TMP_PREFIX);
}

FLASHMEM bool isStagingPath(const char* normalized) {
    return pathStartsWith(normalized, RESOLVED_TMP_PREFIX) &&
           normalized[std::strlen(RESOLVED_TMP_PREFIX)] != '\0';
}

FLASHMEM bool containsFatShortNameAliasSyntax(const char* normalized) {
    // The lexical filesystem interface cannot prove identity through a FAT
    // short alias. Legitimate '~' filenames are intentionally unsupported by
    // conditional mutations for the same reason.
    return normalized != nullptr && std::strchr(normalized, '~') != nullptr;
}

FLASHMEM FileSystemRpcStatus normalizeMutationPath(
    core::persistence::ProductFileService& files,
    const char* path,
    char* normalized,
    size_t normalizedSize
) {
    auto result = files.resolvePath(path, normalized, normalizedSize);
    return result ? FileSystemRpcStatus::OK : mapError(result.error());
}

FLASHMEM FileSystemRpcStatus removeIfExists(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const char* path
) {
    auto result = files.remove(lease, path);
    if (result || result.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
        return FileSystemRpcStatus::OK;
    }
    return mapError(result.error());
}

FLASHMEM FileSystemRpcStatus writeJournal(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const Journal& journal
) {
    uint8_t buffer[JOURNAL_BUFFER_SIZE] = {};
    ByteWriter writer(buffer, sizeof(buffer));
    const char* staging = journal.kind == Kind::REPLACE ? journal.stagingPath : "";
    if (!writer.writeBytes(JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) ||
        !writer.writeU8(JOURNAL_VERSION) ||
        !writer.writeU8(static_cast<uint8_t>(journal.kind)) ||
        !writer.writeU32(journal.operationId) ||
        !writer.writeBytes(journal.expectedSourceSha256, FILESYSTEM_RPC_SHA256_SIZE) ||
        !writer.writeBytes(journal.replacementSha256, FILESYSTEM_RPC_SHA256_SIZE) ||
        !writer.writeString(journal.currentPath, oc::interface::FILESYSTEM_MAX_PATH_LENGTH) ||
        !writer.writeString(staging, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return FileSystemRpcStatus::INVALID_ARGUMENT;
    }
    const uint32_t crc =
        core::persistence::checksum::crc32(buffer, writer.position());
    if (!writer.writeU32(crc)) return FileSystemRpcStatus::INVALID_ARGUMENT;

    auto existingJournal = files.stat(lease, JOURNAL_PATH);
    if (existingJournal) return FileSystemRpcStatus::INVALID_STATE;
    if (existingJournal.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return mapError(existingJournal.error());
    }
    const auto orphanCleanup = removeIfExists(files, lease, JOURNAL_STAGING_PATH);
    if (orphanCleanup != FileSystemRpcStatus::OK) return orphanCleanup;

    // The durable journal name is never opened or truncated directly. A power
    // loss during this stream leaves only an uncommitted staging file, which
    // recovery can discard because no data mutation starts before promotion.
    auto begin = files.beginWrite(
        lease,
        JOURNAL_STAGING_PATH,
        static_cast<uint32_t>(writer.position())
    );
    if (!begin) return mapError(begin.error());
    auto appended = files.appendWrite(lease, buffer, writer.position());
    if (!appended || appended.value() != writer.position()) {
        (void)files.abortWrite(lease);
        (void)files.remove(lease, JOURNAL_STAGING_PATH);
        return appended ? FileSystemRpcStatus::STORAGE_ERROR : mapError(appended.error());
    }
    auto finish = files.finishWrite(lease);
    if (!finish) {
        (void)files.abortWrite(lease);
        (void)files.remove(lease, JOURNAL_STAGING_PATH);
        return mapError(finish.error());
    }
    auto promoted = files.rename(lease, JOURNAL_STAGING_PATH, JOURNAL_PATH);
    if (!promoted) {
        // On an ambiguous rename failure the durable journal, if created,
        // remains the recovery authority. The uncommitted alias is disposable.
        (void)files.remove(lease, JOURNAL_STAGING_PATH);
        return mapError(promoted.error());
    }
    return FileSystemRpcStatus::OK;
}

FLASHMEM FileSystemRpcStatus readJournal(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    Journal& journal,
    bool& present,
    bool& corrupt
) {
    present = false;
    corrupt = false;
    auto info = files.stat(lease, JOURNAL_PATH);
    if (!info) {
        const auto status = mapError(info.error());
        return isNotFound(status) ? FileSystemRpcStatus::OK : status;
    }
    present = true;
    if (info.value().type != oc::interface::FileType::FILE ||
        info.value().sizeBytes < sizeof(JOURNAL_MAGIC) + 1U ||
        info.value().sizeBytes > JOURNAL_BUFFER_SIZE) {
        corrupt = true;
        return FileSystemRpcStatus::STORAGE_ERROR;
    }

    uint8_t buffer[JOURNAL_BUFFER_SIZE] = {};
    uint32_t offset = 0;
    while (offset < info.value().sizeBytes) {
        auto read = files.read(
            lease,
            JOURNAL_PATH,
            offset,
            buffer + offset,
            info.value().sizeBytes - offset
        );
        if (!read) return mapError(read.error());
        if (read.value() == 0 || read.value() > info.value().sizeBytes - offset) {
            corrupt = true;
            return FileSystemRpcStatus::STORAGE_ERROR;
        }
        offset += static_cast<uint32_t>(read.value());
    }

    ByteReader reader(buffer, info.value().sizeBytes);
    const uint8_t* magic = nullptr;
    uint8_t version = 0;
    uint8_t rawKind = 0;
    const uint8_t* expected = nullptr;
    const uint8_t* replacement = nullptr;
    if (!reader.readBytes(magic, sizeof(JOURNAL_MAGIC)) ||
        std::memcmp(magic, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) != 0 ||
        !reader.readU8(version)) {
        corrupt = true;
        return FileSystemRpcStatus::STORAGE_ERROR;
    }
    // A newer durable journal is preserved in place. The handler remains
    // read-only until firmware that understands the transaction is installed.
    if (version != JOURNAL_VERSION) return FileSystemRpcStatus::UNSUPPORTED;
    if (!reader.readU8(rawKind) ||
        (rawKind != static_cast<uint8_t>(Kind::REPLACE) &&
         rawKind != static_cast<uint8_t>(Kind::DELETE)) ||
        !reader.readU32(journal.operationId) ||
        !reader.readBytes(expected, FILESYSTEM_RPC_SHA256_SIZE) ||
        !reader.readBytes(replacement, FILESYSTEM_RPC_SHA256_SIZE) ||
        !reader.readString(
            journal.currentPath,
            sizeof(journal.currentPath),
            oc::interface::FILESYSTEM_MAX_PATH_LENGTH
        ) ||
        !reader.readString(
            journal.stagingPath,
            sizeof(journal.stagingPath),
            oc::interface::FILESYSTEM_MAX_PATH_LENGTH
        )) {
        corrupt = true;
        return FileSystemRpcStatus::STORAGE_ERROR;
    }
    uint32_t storedChecksum = 0;
    if (!reader.readU32(storedChecksum) || reader.remaining() != 0 ||
        storedChecksum != core::persistence::checksum::crc32(
            buffer,
            info.value().sizeBytes - sizeof(uint32_t)
        )) {
        corrupt = true;
        return FileSystemRpcStatus::STORAGE_ERROR;
    }
    journal.kind = static_cast<Kind>(rawKind);
    copyDigest(journal.expectedSourceSha256, expected);
    copyDigest(journal.replacementSha256, replacement);
    if (journal.currentPath[0] == '\0' ||
        (journal.kind == Kind::REPLACE && journal.stagingPath[0] == '\0') ||
        (journal.kind == Kind::DELETE && journal.stagingPath[0] != '\0')) {
        corrupt = true;
        return FileSystemRpcStatus::STORAGE_ERROR;
    }
    return FileSystemRpcStatus::OK;
}

FLASHMEM FileSystemRpcStatus quarantineCorruptJournal(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease
) {
    // Keep one deterministic evidence slot. Current, staging and backup data
    // are not touched while protocol-owned journal metadata is rotated.
    const auto staleCleanup = removeIfExists(files, lease, JOURNAL_QUARANTINE_PATH);
    if (staleCleanup != FileSystemRpcStatus::OK) return staleCleanup;
    auto quarantined = files.rename(lease, JOURNAL_PATH, JOURNAL_QUARANTINE_PATH);
    return quarantined ? FileSystemRpcStatus::OK : mapError(quarantined.error());
}

FLASHMEM ExecutionResult executeJournal(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const Journal& journal
) {
    return journal.kind == Kind::REPLACE
        ? executeReplace(files, lease, journal)
        : executeDelete(files, lease, journal);
}

FLASHMEM FileSystemRpcStatus recoverPendingMutation(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    bool& quarantined
) {
    quarantined = false;
    Journal journal{};
    bool present = false;
    bool corrupt = false;
    const auto loaded = readJournal(files, lease, journal, present, corrupt);
    if (loaded != FileSystemRpcStatus::OK) {
        if (!corrupt) return loaded;
        const auto quarantine = quarantineCorruptJournal(files, lease);
        if (quarantine != FileSystemRpcStatus::OK) return quarantine;
        quarantined = true;
        return removeIfExists(files, lease, JOURNAL_STAGING_PATH);
    }
    const auto stagingCleanup = removeIfExists(files, lease, JOURNAL_STAGING_PATH);
    if (stagingCleanup != FileSystemRpcStatus::OK) return stagingCleanup;
    if (!present) return FileSystemRpcStatus::OK;
    return executeJournal(files, lease, journal).status;
}

}  // namespace core::protocol::filesystem::conditional_mutation

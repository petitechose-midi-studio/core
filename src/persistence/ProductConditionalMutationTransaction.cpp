#include "persistence/ProductConditionalMutationTransaction.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/PersistenceChecksum.hpp"
#include "persistence/ProductConditionalMutationDigest.hpp"
#include "persistence/PersistenceBinaryCodec.hpp"

namespace core::persistence::conditional_mutation {

using oc::type::Error;
using oc::type::ErrorCode;

namespace {

constexpr uint8_t JOURNAL_VERSION = 1;
constexpr uint8_t JOURNAL_MAGIC[] PROGMEM = {'F', 'S', 'T', 'X'};
constexpr size_t JOURNAL_BUFFER_SIZE =
    sizeof(JOURNAL_MAGIC) +
    sizeof(uint8_t) +  // version
    sizeof(uint8_t) +  // kind
    sizeof(uint32_t) +  // operation id
    (2U * SHA256_SIZE) +
    (2U * (sizeof(uint8_t) + oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) +
    sizeof(uint32_t);  // checksum
static_assert(JOURNAL_BUFFER_SIZE == 464U);

FLASHMEM bool isNotFound(Status status) {
    return status == Status::NOT_FOUND;
}

FLASHMEM bool writePath(binary_codec::Writer& writer, const char* path) {
    if (!path) return false;
    const size_t length = std::strlen(path);
    return length <= oc::interface::FILESYSTEM_MAX_PATH_LENGTH &&
           writer.writeU8(static_cast<uint8_t>(length)) &&
           writer.writeBytes(path, static_cast<uint32_t>(length));
}

FLASHMEM bool readPath(
    binary_codec::Reader& reader,
    char* path,
    size_t capacity
) {
    uint8_t length = 0U;
    if (!path || capacity == 0U || !reader.readU8(length) ||
        length > oc::interface::FILESYSTEM_MAX_PATH_LENGTH ||
        static_cast<size_t>(length) >= capacity ||
        !reader.readBytes(path, length)) {
        return false;
    }
    path[length] = '\0';
    return true;
}

FLASHMEM Status removeJournalLast(
    ProductFileService& files,
    const ProductMutationLease& lease
) {
    const auto stagingCleanup = removeIfExists(files, lease, JOURNAL_STAGING_PATH);
    if (stagingCleanup != Status::OK) return stagingCleanup;
    return removeIfExists(files, lease, JOURNAL_PATH);
}

FLASHMEM ExecutionResult executeReplace(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const Journal& journal
) {
    auto current = readDigest(files, lease, journal.currentPath);
    if (current.status == Status::OK &&
        digestEquals(current.sha256, journal.replacementSha256)) {
        if (removeIfExists(files, lease, journal.stagingPath) != Status::OK ||
            removeIfExists(files, lease, BACKUP_PATH) != Status::OK ||
            removeJournalLast(files, lease) != Status::OK) {
            return {Status::STORAGE_ERROR, true};
        }
        return {Status::OK, true};
    }

    if (current.status == Status::OK) {
        if (!digestEquals(current.sha256, journal.expectedSourceSha256)) {
            return {Status::PRECONDITION_FAILED, false};
        }
        auto staging = readDigest(files, lease, journal.stagingPath);
        if (staging.status != Status::OK ||
            !digestEquals(staging.sha256, journal.replacementSha256)) {
            return {
                staging.status == Status::OK
                    ? Status::PRECONDITION_FAILED
                    : staging.status,
                false,
            };
        }
        auto stagingInfo = files.stat(lease, journal.stagingPath);
        if (!stagingInfo || stagingInfo.value().type != oc::interface::FileType::FILE) {
            return {
                stagingInfo ? Status::INVALID_STATE
                            : statusFromError(stagingInfo.error()),
                false,
            };
        }
        auto promoted = commitProductFileTemp(
            files,
            lease,
            journal.currentPath,
            BACKUP_PATH,
            journal.stagingPath,
            stagingInfo.value().sizeBytes,
            staging.crc32
        );
        if (!promoted) return {statusFromError(promoted.error()), false};

        auto committed = readDigest(files, lease, journal.currentPath);
        if (committed.status != Status::OK ||
            !digestEquals(committed.sha256, journal.replacementSha256)) {
            return {Status::STORAGE_ERROR, false};
        }
        if (removeJournalLast(files, lease) != Status::OK) {
            return {Status::STORAGE_ERROR, true};
        }
        return {Status::OK, true};
    }

    if (current.status != Status::NOT_FOUND) {
        return {current.status, false};
    }

    auto backup = readDigest(files, lease, BACKUP_PATH);
    if (backup.status == Status::NOT_FOUND) {
        return {Status::STORAGE_ERROR, false};
    }
    if (backup.status != Status::OK ||
        !digestEquals(backup.sha256, journal.expectedSourceSha256)) {
        return {
            backup.status == Status::OK
                ? Status::PRECONDITION_FAILED
                : backup.status,
            false,
        };
    }

    auto staging = readDigest(files, lease, journal.stagingPath);
    if (staging.status == Status::OK &&
        digestEquals(staging.sha256, journal.replacementSha256)) {
        auto stagingInfo = files.stat(lease, journal.stagingPath);
        if (!stagingInfo || stagingInfo.value().type != oc::interface::FileType::FILE) {
            return {
                stagingInfo ? Status::INVALID_STATE
                            : statusFromError(stagingInfo.error()),
                false,
            };
        }
        auto promoted = commitProductFileTemp(
            files,
            lease,
            journal.currentPath,
            BACKUP_PATH,
            journal.stagingPath,
            stagingInfo.value().sizeBytes,
            staging.crc32
        );
        if (!promoted) return {statusFromError(promoted.error()), false};
        if (removeJournalLast(files, lease) != Status::OK) {
            return {Status::STORAGE_ERROR, true};
        }
        return {Status::OK, true};
    }

    auto restored = files.rename(lease, BACKUP_PATH, journal.currentPath);
    if (!restored) return {statusFromError(restored.error()), false};
    if (removeJournalLast(files, lease) != Status::OK) {
        return {Status::STORAGE_ERROR, false};
    }
    return {Status::STORAGE_ERROR, false};
}

FLASHMEM ExecutionResult executeDelete(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const Journal& journal
) {
    auto current = readDigest(files, lease, journal.currentPath);
    if (current.status == Status::OK) {
        if (!digestEquals(current.sha256, journal.expectedSourceSha256)) {
            return {Status::PRECONDITION_FAILED, false};
        }
        auto backup = files.stat(lease, BACKUP_PATH);
        if (backup || backup.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
            return {Status::INVALID_STATE, false};
        }
        auto moved = files.rename(lease, journal.currentPath, BACKUP_PATH);
        if (!moved) return {statusFromError(moved.error()), false};
    } else if (current.status != Status::NOT_FOUND) {
        return {current.status, false};
    }

    auto backup = readDigest(files, lease, BACKUP_PATH);
    if (backup.status == Status::OK) {
        if (!digestEquals(backup.sha256, journal.expectedSourceSha256)) {
            return {Status::PRECONDITION_FAILED, false};
        }
        auto removed = files.remove(lease, BACKUP_PATH);
        if (!removed) return {statusFromError(removed.error()), false};
    } else if (backup.status != Status::NOT_FOUND) {
        return {backup.status, false};
    }

    if (removeJournalLast(files, lease) != Status::OK) {
        return {Status::STORAGE_ERROR, true};
    }
    return {Status::OK, true};
}

}  // namespace

FLASHMEM Status statusFromError(Error error) {
    switch (error.code) {
        case ErrorCode::OK:
            return Status::OK;
        case ErrorCode::INVALID_ARGUMENT:
            return Status::INVALID_ARGUMENT;
        case ErrorCode::RESOURCE_NOT_FOUND:
            return Status::NOT_FOUND;
        case ErrorCode::HARDWARE_BUSY:
            return Status::BUSY;
        case ErrorCode::RESOURCE_EXHAUSTED:
            return Status::TOO_LARGE;
        case ErrorCode::HARDWARE_NOT_FOUND:
        case ErrorCode::HARDWARE_INIT_FAILED:
        case ErrorCode::HARDWARE_TIMEOUT:
        case ErrorCode::STORAGE_READ_FAILED:
        case ErrorCode::STORAGE_WRITE_FAILED:
        case ErrorCode::STORAGE_CORRUPT:
            return Status::STORAGE_ERROR;
        case ErrorCode::INVALID_STATE:
        default:
            return Status::INVALID_STATE;
    }
}

FLASHMEM ErrorCode recoveryError(Status status) {
    switch (status) {
        case Status::BUSY:
            return ErrorCode::HARDWARE_BUSY;
        case Status::TOO_LARGE:
            return ErrorCode::RESOURCE_EXHAUSTED;
        case Status::NOT_FOUND:
        case Status::STORAGE_ERROR:
            return ErrorCode::STORAGE_WRITE_FAILED;
        case Status::UNSUPPORTED:
        case Status::PRECONDITION_FAILED:
        case Status::INVALID_ARGUMENT:
        case Status::INVALID_STATE:
            return ErrorCode::INVALID_STATE;
        case Status::OK:
        default:
            return ErrorCode::OK;
    }
}

FLASHMEM Status removeIfExists(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path
) {
    auto result = files.remove(lease, path);
    if (result || result.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
        return Status::OK;
    }
    return statusFromError(result.error());
}

FLASHMEM Status writeJournal(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const Journal& journal
) {
    uint8_t buffer[JOURNAL_BUFFER_SIZE] = {};
    binary_codec::Writer writer(buffer, sizeof(buffer));
    const char* staging = journal.kind == Kind::REPLACE ? journal.stagingPath : "";
    if (!writer.writeBytes(JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) ||
        !writer.writeU8(JOURNAL_VERSION) ||
        !writer.writeU8(static_cast<uint8_t>(journal.kind)) ||
        !writer.writeU32(journal.operationId) ||
        !writer.writeBytes(journal.expectedSourceSha256, SHA256_SIZE) ||
        !writer.writeBytes(journal.replacementSha256, SHA256_SIZE) ||
        !writePath(writer, journal.currentPath) ||
        !writePath(writer, staging)) {
        return Status::INVALID_ARGUMENT;
    }
    const uint32_t crc =
        checksum::crc32(buffer, writer.offset());
    if (!writer.writeU32(crc)) return Status::INVALID_ARGUMENT;

    auto existingJournal = files.stat(lease, JOURNAL_PATH);
    if (existingJournal) return Status::INVALID_STATE;
    if (existingJournal.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return statusFromError(existingJournal.error());
    }
    const auto orphanCleanup = removeIfExists(files, lease, JOURNAL_STAGING_PATH);
    if (orphanCleanup != Status::OK) return orphanCleanup;

    // The durable journal name is never opened or truncated directly. A power
    // loss during this stream leaves only an uncommitted staging file, which
    // recovery can discard because no data mutation starts before promotion.
    auto begin = files.beginWrite(
        lease,
        JOURNAL_STAGING_PATH,
        writer.offset()
    );
    if (!begin) return statusFromError(begin.error());
    auto appended = files.appendWrite(lease, buffer, writer.offset());
    if (!appended || appended.value() != writer.offset()) {
        (void)files.abortWrite(lease);
        (void)files.remove(lease, JOURNAL_STAGING_PATH);
        return appended ? Status::STORAGE_ERROR : statusFromError(appended.error());
    }
    auto finish = files.finishWrite(lease);
    if (!finish) {
        (void)files.abortWrite(lease);
        (void)files.remove(lease, JOURNAL_STAGING_PATH);
        return statusFromError(finish.error());
    }
    auto promoted = files.rename(lease, JOURNAL_STAGING_PATH, JOURNAL_PATH);
    if (!promoted) {
        // On an ambiguous rename failure the durable journal, if created,
        // remains the recovery authority. The uncommitted alias is disposable.
        (void)files.remove(lease, JOURNAL_STAGING_PATH);
        return statusFromError(promoted.error());
    }
    return Status::OK;
}

FLASHMEM Status readJournal(
    ProductFileService& files,
    const ProductMutationLease& lease,
    Journal& journal,
    bool& present,
    bool& corrupt
) {
    present = false;
    corrupt = false;
    auto info = files.stat(lease, JOURNAL_PATH);
    if (!info) {
        const auto status = statusFromError(info.error());
        return isNotFound(status) ? Status::OK : status;
    }
    present = true;
    if (info.value().type != oc::interface::FileType::FILE ||
        info.value().sizeBytes < sizeof(JOURNAL_MAGIC) + 1U ||
        info.value().sizeBytes > JOURNAL_BUFFER_SIZE) {
        corrupt = true;
        return Status::STORAGE_ERROR;
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
        if (!read) return statusFromError(read.error());
        if (read.value() == 0 || read.value() > info.value().sizeBytes - offset) {
            corrupt = true;
            return Status::STORAGE_ERROR;
        }
        offset += static_cast<uint32_t>(read.value());
    }

    binary_codec::Reader reader(buffer, info.value().sizeBytes);
    uint8_t magic[sizeof(JOURNAL_MAGIC)] = {};
    uint8_t version = 0;
    uint8_t rawKind = 0;
    if (!reader.readBytes(magic, sizeof(JOURNAL_MAGIC)) ||
        std::memcmp(magic, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) != 0 ||
        !reader.readU8(version)) {
        corrupt = true;
        return Status::STORAGE_ERROR;
    }
    // A newer durable journal is preserved in place. The handler remains
    // read-only until firmware that understands the transaction is installed.
    if (version != JOURNAL_VERSION) return Status::UNSUPPORTED;
    if (!reader.readU8(rawKind) ||
        (rawKind != static_cast<uint8_t>(Kind::REPLACE) &&
         rawKind != static_cast<uint8_t>(Kind::DELETE)) ||
        !reader.readU32(journal.operationId) ||
        !reader.readBytes(journal.expectedSourceSha256, SHA256_SIZE) ||
        !reader.readBytes(journal.replacementSha256, SHA256_SIZE) ||
        !readPath(reader, journal.currentPath, sizeof(journal.currentPath)) ||
        !readPath(reader, journal.stagingPath, sizeof(journal.stagingPath))) {
        corrupt = true;
        return Status::STORAGE_ERROR;
    }
    uint32_t storedChecksum = 0;
    if (!reader.readU32(storedChecksum) || reader.remaining() != 0 ||
        storedChecksum != checksum::crc32(
            buffer,
            info.value().sizeBytes - sizeof(uint32_t)
        )) {
        corrupt = true;
        return Status::STORAGE_ERROR;
    }
    journal.kind = static_cast<Kind>(rawKind);
    if (journal.currentPath[0] == '\0' ||
        (journal.kind == Kind::REPLACE && journal.stagingPath[0] == '\0') ||
        (journal.kind == Kind::DELETE && journal.stagingPath[0] != '\0')) {
        corrupt = true;
        return Status::STORAGE_ERROR;
    }
    return Status::OK;
}

FLASHMEM Status quarantineCorruptJournal(
    ProductFileService& files,
    const ProductMutationLease& lease
) {
    // Keep one deterministic evidence slot. Current, staging and backup data
    // are not touched while conditional journal metadata is rotated.
    const auto staleCleanup = removeIfExists(files, lease, JOURNAL_QUARANTINE_PATH);
    if (staleCleanup != Status::OK) return staleCleanup;
    auto quarantined = files.rename(lease, JOURNAL_PATH, JOURNAL_QUARANTINE_PATH);
    return quarantined ? Status::OK : statusFromError(quarantined.error());
}

FLASHMEM ExecutionResult executeJournal(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const Journal& journal
) {
    return journal.kind == Kind::REPLACE
        ? executeReplace(files, lease, journal)
        : executeDelete(files, lease, journal);
}

FLASHMEM Status recoverPendingMutation(
    ProductFileService& files,
    const ProductMutationLease& lease,
    bool& quarantined
) {
    quarantined = false;
    Journal journal{};
    bool present = false;
    bool corrupt = false;
    const auto loaded = readJournal(files, lease, journal, present, corrupt);
    if (loaded != Status::OK) {
        if (!corrupt) return loaded;
        const auto quarantine = quarantineCorruptJournal(files, lease);
        if (quarantine != Status::OK) return quarantine;
        quarantined = true;
        return removeIfExists(files, lease, JOURNAL_STAGING_PATH);
    }
    const auto stagingCleanup = removeIfExists(files, lease, JOURNAL_STAGING_PATH);
    if (stagingCleanup != Status::OK) return stagingCleanup;
    if (!present) return Status::OK;
    return executeJournal(files, lease, journal).status;
}

}  // namespace core::persistence::conditional_mutation

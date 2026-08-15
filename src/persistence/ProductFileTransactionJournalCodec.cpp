#include "persistence/ProductFileTransactionJournalInternal.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceChecksum.hpp"

namespace core::persistence::product_file_transaction {

namespace {

using oc::type::ErrorCode;

constexpr uint8_t JOURNAL_MAGIC[] PROGMEM = {'P', 'F', 'T', 'X'};
constexpr uint8_t FLAG_HAD_CURRENT = 0x01U;
constexpr size_t JOURNAL_PREFIX_SIZE = sizeof(JOURNAL_MAGIC) + 1U;
constexpr size_t JOURNAL_HEADER_SIZE = 24U;
constexpr size_t JOURNAL_CHECKSUM_SIZE = sizeof(uint32_t);
constexpr size_t JOURNAL_MIN_RECORD_SIZE =
    JOURNAL_HEADER_SIZE + 3U * 2U + JOURNAL_CHECKSUM_SIZE;
constexpr char RESOLVED_JOURNAL_SLOT_A[] =
    "/midi-studio/tmp/rpc-product-file-a.journal";
constexpr char RESOLVED_JOURNAL_SLOT_B[] =
    "/midi-studio/tmp/rpc-product-file-b.journal";

static_assert(PRODUCT_FILE_JOURNAL_MAX_RECORD_SIZE ==
              JOURNAL_HEADER_SIZE +
                  3U * (1U + oc::interface::FILESYSTEM_MAX_PATH_LENGTH) +
                  JOURNAL_CHECKSUM_SIZE);

constexpr bool phaseValid(uint8_t raw) {
    return raw >= static_cast<uint8_t>(ProductFileTransactionPhase::PREPARED) &&
           raw <= static_cast<uint8_t>(ProductFileTransactionPhase::ROLLED_BACK);
}

constexpr char asciiLower(char value) {
    return value >= 'A' && value <= 'Z'
        ? static_cast<char>(value + ('a' - 'A'))
        : value;
}

FLASHMEM bool pathEqualsFat(const char* lhs, const char* rhs) {
    if (lhs == nullptr || rhs == nullptr) return false;
    while (*lhs != '\0' && *rhs != '\0') {
        if (asciiLower(*lhs) != asciiLower(*rhs)) return false;
        ++lhs;
        ++rhs;
    }
    return *lhs == '\0' && *rhs == '\0';
}

constexpr const char* slotPath_(uint8_t slot) {
    return slot == 0U ? PRODUCT_FILE_JOURNAL_SLOT_A : PRODUCT_FILE_JOURNAL_SLOT_B;
}

constexpr uint8_t inactiveSlot(uint8_t active) {
    return active == 0U ? 1U : 0U;
}

FLASHMEM void writeU32LE(uint8_t* out, uint32_t value) {
    out[0] = static_cast<uint8_t>(value & 0xFFU);
    out[1] = static_cast<uint8_t>((value >> 8U) & 0xFFU);
    out[2] = static_cast<uint8_t>((value >> 16U) & 0xFFU);
    out[3] = static_cast<uint8_t>((value >> 24U) & 0xFFU);
}

FLASHMEM void writeU64LE(uint8_t* out, uint64_t value) {
    for (uint8_t index = 0; index < 8U; ++index) {
        out[index] = static_cast<uint8_t>((value >> (8U * index)) & 0xFFU);
    }
}

FLASHMEM uint32_t readU32LE(const uint8_t* data) {
    return static_cast<uint32_t>(data[0]) |
           (static_cast<uint32_t>(data[1]) << 8U) |
           (static_cast<uint32_t>(data[2]) << 16U) |
           (static_cast<uint32_t>(data[3]) << 24U);
}

FLASHMEM uint64_t readU64LE(const uint8_t* data) {
    uint64_t value = 0;
    for (uint8_t index = 0; index < 8U; ++index) {
        value |= static_cast<uint64_t>(data[index]) << (8U * index);
    }
    return value;
}

FLASHMEM oc::type::Result<void> appendExact(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const uint8_t* data,
    size_t size
) {
    auto appended = files.appendWrite(lease, data, size);
    if (!appended) return oc::type::Result<void>::err(appended.error());
    if (appended.value() != size) {
        return oc::type::Result<void>::err(
            {ErrorCode::STORAGE_WRITE_FAILED, "short product file journal write"}
        );
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> validateStoredPaths(
    ProductFileService& files,
    const JournalWorkspace& workspace
) {
    char normalized[PATH_CAPACITY] = {};
    for (uint8_t index = 0; index < PATH_COUNT; ++index) {
        const char* path = workspace.storage.paths[index];
        if (std::strchr(path, '~') != nullptr) {
            return oc::type::Result<void>::err(
                {ErrorCode::INVALID_ARGUMENT, "product file journal alias path"}
            );
        }
        auto resolved = files.resolvePath(path, normalized, sizeof(normalized));
        if (!resolved || std::strcmp(path, normalized) != 0 || isJournalPath(path)) {
            return oc::type::Result<void>::err(
                {ErrorCode::INVALID_ARGUMENT, "noncanonical product file journal path"}
            );
        }
    }
    for (uint8_t lhs = 0; lhs < PATH_COUNT; ++lhs) {
        for (uint8_t rhs = static_cast<uint8_t>(lhs + 1U); rhs < PATH_COUNT; ++rhs) {
            if (pathEqualsFat(
                    workspace.storage.paths[lhs],
                    workspace.storage.paths[rhs]
                )) {
                return oc::type::Result<void>::err(
                    {ErrorCode::INVALID_ARGUMENT, "aliased product file journal paths"}
                );
            }
        }
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<JournalSlotObservation> readSlot_(
    ProductFileService& files,
    const ProductMutationLease& lease,
    uint8_t slot,
    JournalWorkspace& workspace
) {
    auto info = files.stat(lease, slotPath_(slot));
    if (!info) {
        if (info.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
            return oc::type::Result<JournalSlotObservation>::ok(
                {JournalSlotState::ABSENT, 0}
            );
        }
        return oc::type::Result<JournalSlotObservation>::err(info.error());
    }
    if (info.value().type != oc::interface::FileType::FILE ||
        info.value().sizeBytes < JOURNAL_PREFIX_SIZE ||
        info.value().sizeBytes > PRODUCT_FILE_JOURNAL_MAX_RECORD_SIZE) {
        return oc::type::Result<JournalSlotObservation>::ok(
            {JournalSlotState::CORRUPT, 0}
        );
    }

    auto read = files.read(
        lease,
        slotPath_(slot),
        0,
        workspace.storage.encoded,
        info.value().sizeBytes
    );
    if (!read) return oc::type::Result<JournalSlotObservation>::err(read.error());
    if (read.value() != info.value().sizeBytes) {
        return oc::type::Result<JournalSlotObservation>::err(
            {ErrorCode::STORAGE_READ_FAILED, "short product file journal read"}
        );
    }

    const uint8_t* encoded = workspace.storage.encoded;
    if (std::memcmp(encoded, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC)) != 0) {
        return oc::type::Result<JournalSlotObservation>::ok(
            {JournalSlotState::CORRUPT, 0}
        );
    }
    const uint8_t version = encoded[4];
    if (version != PRODUCT_FILE_JOURNAL_VERSION) {
        return oc::type::Result<JournalSlotObservation>::ok(
            {JournalSlotState::UNSUPPORTED, 0}
        );
    }
    if (info.value().sizeBytes < JOURNAL_MIN_RECORD_SIZE) {
        return oc::type::Result<JournalSlotObservation>::ok(
            {JournalSlotState::CORRUPT, 0}
        );
    }
    const uint32_t storedChecksum = readU32LE(
        encoded + info.value().sizeBytes - JOURNAL_CHECKSUM_SIZE
    );
    if (storedChecksum != checksum::crc32(
            encoded,
            info.value().sizeBytes - JOURNAL_CHECKSUM_SIZE
        )) {
        return oc::type::Result<JournalSlotObservation>::ok(
            {JournalSlotState::CORRUPT, 0}
        );
    }

    const uint8_t rawPhase = encoded[5];
    const uint8_t flags = encoded[6];
    if (!phaseValid(rawPhase) || (flags & ~FLAG_HAD_CURRENT) != 0U ||
        encoded[7] != 0U) {
        return oc::type::Result<JournalSlotObservation>::ok(
            {JournalSlotState::CORRUPT, 0}
        );
    }
    const uint64_t sequence = readU64LE(encoded + 8U);
    const uint32_t expectedSize = readU32LE(encoded + 16U);
    const uint32_t expectedCrc32 = readU32LE(encoded + 20U);
    if (sequence == 0U) {
        return oc::type::Result<JournalSlotObservation>::ok(
            {JournalSlotState::CORRUPT, 0}
        );
    }

    size_t offsets[PATH_COUNT] = {};
    uint8_t lengths[PATH_COUNT] = {};
    size_t cursor = JOURNAL_HEADER_SIZE;
    const size_t payloadEnd = info.value().sizeBytes - JOURNAL_CHECKSUM_SIZE;
    for (uint8_t index = 0; index < PATH_COUNT; ++index) {
        if (cursor >= payloadEnd) {
            return oc::type::Result<JournalSlotObservation>::ok(
                {JournalSlotState::CORRUPT, 0}
            );
        }
        const uint8_t length = encoded[cursor++];
        if (length == 0U || length > oc::interface::FILESYSTEM_MAX_PATH_LENGTH ||
            cursor + length > payloadEnd) {
            return oc::type::Result<JournalSlotObservation>::ok(
                {JournalSlotState::CORRUPT, 0}
            );
        }
        lengths[index] = length;
        offsets[index] = cursor;
        cursor += length;
    }
    if (cursor != payloadEnd) {
        return oc::type::Result<JournalSlotObservation>::ok(
            {JournalSlotState::CORRUPT, 0}
        );
    }

    for (int index = PATH_COUNT - 1; index >= 0; --index) {
        std::memmove(
            workspace.storage.paths[index],
            workspace.storage.encoded + offsets[index],
            lengths[index]
        );
        workspace.storage.paths[index][lengths[index]] = '\0';
    }
    workspace.phase = static_cast<ProductFileTransactionPhase>(rawPhase);
    workspace.hadCurrent = (flags & FLAG_HAD_CURRENT) != 0U;
    workspace.sequence = sequence;
    workspace.expectedSize = expectedSize;
    workspace.expectedCrc32 = expectedCrc32;
    workspace.activeSlot = slot;

    auto validPaths = validateStoredPaths(files, workspace);
    if (!validPaths) {
        return oc::type::Result<JournalSlotObservation>::ok(
            {JournalSlotState::CORRUPT, 0}
        );
    }
    return oc::type::Result<JournalSlotObservation>::ok(
        {JournalSlotState::VALID, sequence}
    );
}

}  // namespace

FLASHMEM bool isJournalPath(const char* normalized) {
    return pathEqualsFat(normalized, RESOLVED_JOURNAL_SLOT_A) ||
           pathEqualsFat(normalized, RESOLVED_JOURNAL_SLOT_B);
}

FLASHMEM const char* journalSlotPath(uint8_t slot) {
    return slotPath_(slot);
}

FLASHMEM oc::type::Result<JournalSlotObservation> readJournalSlot(
    ProductFileService& files,
    const ProductMutationLease& lease,
    uint8_t slot,
    JournalWorkspace& workspace
) {
    if (slot > 1U) {
        return oc::type::Result<JournalSlotObservation>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid product journal slot"}
        );
    }
    return readSlot_(files, lease, slot, workspace);
}

FLASHMEM oc::type::Result<void> persistPhase(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace,
    ProductFileTransactionPhase phase
) {
    if (workspace.sequence == UINT64_MAX) {
        return oc::type::Result<void>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "product file journal sequence exhausted"}
        );
    }

    uint8_t pathLengths[PATH_COUNT] = {};
    size_t recordSize = JOURNAL_HEADER_SIZE + JOURNAL_CHECKSUM_SIZE;
    for (uint8_t index = 0; index < PATH_COUNT; ++index) {
        const size_t length = std::strlen(workspace.storage.paths[index]);
        if (length == 0U || length > oc::interface::FILESYSTEM_MAX_PATH_LENGTH) {
            return oc::type::Result<void>::err(
                {ErrorCode::INVALID_ARGUMENT, "invalid product file journal path"}
            );
        }
        pathLengths[index] = static_cast<uint8_t>(length);
        recordSize += 1U + length;
    }
    if (recordSize > PRODUCT_FILE_JOURNAL_MAX_RECORD_SIZE) {
        return oc::type::Result<void>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "product file journal record too large"}
        );
    }

    const uint64_t nextSequence = workspace.sequence + 1U;
    const uint8_t targetSlot = workspace.activeSlot == NO_ACTIVE_SLOT
        ? 0U
        : inactiveSlot(workspace.activeSlot);
    uint8_t record[PRODUCT_FILE_JOURNAL_MAX_RECORD_SIZE] = {};
    std::memcpy(record, JOURNAL_MAGIC, sizeof(JOURNAL_MAGIC));
    record[4] = PRODUCT_FILE_JOURNAL_VERSION;
    record[5] = static_cast<uint8_t>(phase);
    record[6] = workspace.hadCurrent ? FLAG_HAD_CURRENT : 0U;
    writeU64LE(record + 8U, nextSequence);
    writeU32LE(record + 16U, workspace.expectedSize);
    writeU32LE(record + 20U, workspace.expectedCrc32);

    size_t cursor = JOURNAL_HEADER_SIZE;
    for (uint8_t index = 0; index < PATH_COUNT; ++index) {
        const uint8_t length = pathLengths[index];
        record[cursor++] = length;
        std::memcpy(record + cursor, workspace.storage.paths[index], length);
        cursor += length;
    }
    writeU32LE(
        record + cursor,
        checksum::crc32(record, cursor)
    );
    cursor += JOURNAL_CHECKSUM_SIZE;
    if (cursor != recordSize) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "product file journal size drift"}
        );
    }

    auto begun = files.beginWrite(
        lease,
        slotPath_(targetSlot),
        static_cast<uint32_t>(recordSize)
    );
    if (!begun) return begun;

    auto appended = appendExact(files, lease, record, recordSize);
    if (!appended) {
        (void)files.abortWrite(lease);
        return appended;
    }

    auto finished = files.finishWrite(lease);
    if (!finished) {
        (void)files.abortWrite(lease);
        return finished;
    }
    auto flushed = files.flush(lease, slotPath_(targetSlot));
    if (!flushed) return flushed;

    workspace.sequence = nextSequence;
    workspace.phase = phase;
    workspace.activeSlot = targetSlot;
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<JournalSelection> selectLatest(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace
) {
    auto firstRead = readSlot_(files, lease, 0U, workspace);
    if (!firstRead) return oc::type::Result<JournalSelection>::err(firstRead.error());
    const JournalSlotObservation first = firstRead.value();

    auto secondRead = readSlot_(files, lease, 1U, workspace);
    if (!secondRead) return oc::type::Result<JournalSelection>::err(secondRead.error());
    const JournalSlotObservation second = secondRead.value();

    if (first.state == JournalSlotState::UNSUPPORTED ||
        second.state == JournalSlotState::UNSUPPORTED) {
        return oc::type::Result<JournalSelection>::err(
            {ErrorCode::INVALID_STATE, "unsupported product file journal version"}
        );
    }

    const bool firstValid = first.state == JournalSlotState::VALID;
    const bool secondValid = second.state == JournalSlotState::VALID;
    if (firstValid && secondValid && first.sequence == second.sequence) {
        return oc::type::Result<JournalSelection>::err(
            {ErrorCode::STORAGE_CORRUPT, "ambiguous product file journal sequence"}
        );
    }
    if (firstValid || secondValid) {
        const uint8_t selected = !secondValid ||
                                 (firstValid && first.sequence > second.sequence)
            ? 0U
            : 1U;
        auto selectedRead = readSlot_(files, lease, selected, workspace);
        if (!selectedRead ||
            selectedRead.value().state != JournalSlotState::VALID) {
            return selectedRead
                ? oc::type::Result<JournalSelection>::err(
                      {ErrorCode::STORAGE_CORRUPT, "product file journal changed during selection"}
                  )
                : oc::type::Result<JournalSelection>::err(selectedRead.error());
        }
        return oc::type::Result<JournalSelection>::ok({true});
    }

    const bool firstCorrupt = first.state == JournalSlotState::CORRUPT;
    const bool secondCorrupt = second.state == JournalSlotState::CORRUPT;
    if (firstCorrupt && secondCorrupt) {
        return oc::type::Result<JournalSelection>::err(
            {ErrorCode::STORAGE_CORRUPT, "both product file journal slots corrupt"}
        );
    }
    if (firstCorrupt || secondCorrupt) {
        const uint8_t corruptSlot = firstCorrupt ? 0U : 1U;
        auto removed = deleteProductFileIfExists(
            files,
            lease,
            slotPath_(corruptSlot)
        );
        if (!removed) return oc::type::Result<JournalSelection>::err(removed.error());
    }
    workspace = JournalWorkspace{};
    return oc::type::Result<JournalSelection>::ok({false});
}

FLASHMEM oc::type::Result<void> normalizePaths(
    ProductFileService& files,
    JournalWorkspace& workspace,
    const char* current,
    const char* tmp,
    const char* backup
) {
    const char* inputs[PATH_COUNT] = {current, tmp, backup};
    for (uint8_t index = 0; index < PATH_COUNT; ++index) {
        auto resolved = files.resolvePath(
            inputs[index],
            workspace.storage.paths[index],
            PATH_CAPACITY
        );
        if (!resolved) return resolved;
    }
    return validateStoredPaths(files, workspace);
}

FLASHMEM oc::type::Result<FileState> inspectFile(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path
) {
    auto info = files.stat(lease, path);
    if (!info) {
        if (info.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
            return oc::type::Result<FileState>::ok({false, 0});
        }
        return oc::type::Result<FileState>::err(info.error());
    }
    if (info.value().type != oc::interface::FileType::FILE) {
        return oc::type::Result<FileState>::err(
            {ErrorCode::INVALID_STATE, "product file transaction path is not a file"}
        );
    }
    return oc::type::Result<FileState>::ok({true, info.value().sizeBytes});
}

FLASHMEM RecoveryAction decideRecovery(
    const JournalWorkspace& workspace,
    FileState final,
    bool finalValid,
    FileState tmp,
    bool tmpValid,
    FileState backup
) {
    if (workspace.phase == ProductFileTransactionPhase::ROLLED_BACK) {
        if (workspace.hadCurrent) {
            if (backup.exists) {
                return final.exists
                    ? RecoveryAction::REMOVE_CURRENT_AND_RESTORE_BACKUP
                    : RecoveryAction::RESTORE_BACKUP;
            }
            return final.exists ? RecoveryAction::FINISH_ROLLED_BACK
                                : RecoveryAction::FAIL_CORRUPT;
        }
        return final.exists ? RecoveryAction::REMOVE_CURRENT_AND_ROLL_BACK
                            : RecoveryAction::FINISH_ROLLED_BACK;
    }

    if (workspace.phase == ProductFileTransactionPhase::COMMITTED) {
        if (finalValid) return RecoveryAction::FINISH_COMMITTED;
        if (workspace.hadCurrent && backup.exists) {
            return final.exists
                ? RecoveryAction::REMOVE_CURRENT_AND_RESTORE_BACKUP
                : RecoveryAction::RESTORE_BACKUP;
        }
        if (!workspace.hadCurrent) {
            return final.exists ? RecoveryAction::REMOVE_CURRENT_AND_ROLL_BACK
                                : RecoveryAction::FINISH_ROLLED_BACK;
        }
        return RecoveryAction::FAIL_CORRUPT;
    }

    if (final.exists && tmp.exists) {
        if (workspace.phase == ProductFileTransactionPhase::PREPARED &&
            workspace.hadCurrent && !backup.exists) {
            return tmpValid
                ? RecoveryAction::BACK_UP_CURRENT_AND_PROMOTE_TMP
                : RecoveryAction::FINISH_ROLLED_BACK;
        }
        if (workspace.hadCurrent && backup.exists) {
            return RecoveryAction::REMOVE_CURRENT_AND_RESTORE_BACKUP;
        }
        return !workspace.hadCurrent
            ? RecoveryAction::REMOVE_CURRENT_AND_ROLL_BACK
            : RecoveryAction::FAIL_CORRUPT;
    }

    if (!final.exists && tmp.exists) {
        if (tmpValid) return RecoveryAction::PROMOTE_TMP;
        if (workspace.hadCurrent && backup.exists) {
            return RecoveryAction::RESTORE_BACKUP;
        }
        return !workspace.hadCurrent ? RecoveryAction::FINISH_ROLLED_BACK
                                     : RecoveryAction::FAIL_CORRUPT;
    }

    if (final.exists && !tmp.exists) {
        if (workspace.hadCurrent && backup.exists) {
            return finalValid
                ? RecoveryAction::FINISH_COMMITTED
                : RecoveryAction::REMOVE_CURRENT_AND_RESTORE_BACKUP;
        }
        if (workspace.phase == ProductFileTransactionPhase::PREPARED &&
            workspace.hadCurrent) {
            return RecoveryAction::FINISH_ROLLED_BACK;
        }
        if (finalValid) return RecoveryAction::FINISH_COMMITTED;
        return !workspace.hadCurrent
            ? RecoveryAction::REMOVE_CURRENT_AND_ROLL_BACK
            : RecoveryAction::FAIL_CORRUPT;
    }

    if (workspace.hadCurrent && backup.exists) {
        return RecoveryAction::RESTORE_BACKUP;
    }
    return !workspace.hadCurrent ? RecoveryAction::FINISH_ROLLED_BACK
                                 : RecoveryAction::FAIL_CORRUPT;
}

FLASHMEM oc::type::Result<void> cleanupMappedPath(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path
) {
    return deleteProductFileIfExists(files, lease, path);
}

}  // namespace core::persistence::product_file_transaction

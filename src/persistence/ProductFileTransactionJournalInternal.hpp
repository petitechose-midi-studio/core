#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "persistence/AtomicProductFile.hpp"

namespace core::persistence::product_file_transaction {

inline constexpr size_t PATH_CAPACITY =
    oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1U;
inline constexpr uint8_t NO_ACTIVE_SLOT = 0xFFU;

enum PathIndex : uint8_t {
    FINAL_PATH = 0,
    TMP_PATH = 1,
    BACKUP_PATH = 2,
    PATH_COUNT = 3,
};

union JournalStorage {
    uint8_t encoded[PRODUCT_FILE_JOURNAL_MAX_RECORD_SIZE];
    char paths[PATH_COUNT][PATH_CAPACITY];
};

struct JournalWorkspace {
    JournalStorage storage{};
    uint64_t sequence = 0;
    uint32_t expectedSize = 0;
    ProductFileTransactionPhase phase = ProductFileTransactionPhase::NONE;
    uint8_t activeSlot = NO_ACTIVE_SLOT;
    bool hadCurrent = false;

    char* path(PathIndex index) { return storage.paths[index]; }
    const char* path(PathIndex index) const { return storage.paths[index]; }
};

struct JournalSelection {
    bool present = false;
};

struct FileState {
    bool exists = false;
    uint32_t size = 0;
};

constexpr bool phaseTerminal(ProductFileTransactionPhase phase) {
    return phase == ProductFileTransactionPhase::COMMITTED ||
           phase == ProductFileTransactionPhase::ROLLED_BACK;
}

oc::type::Result<void> persistPhase(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace,
    ProductFileTransactionPhase phase
);

oc::type::Result<JournalSelection> selectLatest(
    ProductFileService& files,
    const ProductMutationLease& lease,
    JournalWorkspace& workspace
);

oc::type::Result<void> normalizePaths(
    ProductFileService& files,
    JournalWorkspace& workspace,
    const char* current,
    const char* tmp,
    const char* backup
);

oc::type::Result<FileState> inspectFile(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path
);

oc::type::Result<void> cleanupMappedPath(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path
);

bool isJournalPath(const char* normalized);

}  // namespace core::persistence::product_file_transaction

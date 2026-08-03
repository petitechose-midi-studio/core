#pragma once

#include <cstdint>

#include <oc/type/Result.hpp>

#include "persistence/ProductFileService.hpp"

namespace core::persistence {

struct AtomicProductFilePaths {
    const char* directory = nullptr;
    const char* current = nullptr;
    const char* backup = nullptr;
    const char* tmp = nullptr;
};

inline constexpr const char* PRODUCT_FILE_JOURNAL_SLOT_A =
    "tmp/rpc-product-file-a.journal";
inline constexpr const char* PRODUCT_FILE_JOURNAL_SLOT_B =
    "tmp/rpc-product-file-b.journal";
inline constexpr uint8_t PRODUCT_FILE_JOURNAL_VERSION = 1U;
inline constexpr uint16_t PRODUCT_FILE_JOURNAL_MAX_RECORD_SIZE = 603U;

enum class ProductFileTransactionPhase : uint8_t {
    NONE = 0,
    PREPARED,
    BACKED_UP,
    PROMOTED,
    COMMITTED,
    ROLLED_BACK,
};

oc::type::Result<void> deleteProductFileIfExists(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path
);

oc::type::Result<void> writeProductFileTemp(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* tmpPath,
    const uint8_t* data,
    uint32_t size,
    uint32_t chunkSize
);

oc::type::Result<void> commitProductFileTemp(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* current,
    const char* backup,
    const char* tmp,
    uint32_t expectedSize
);

oc::type::Result<void> replaceProductFileAtomically(
    ProductFileService& files,
    const ProductMutationLease& lease,
    AtomicProductFilePaths paths,
    const uint8_t* data,
    uint32_t size,
    uint32_t chunkSize
);

oc::type::Result<bool> recoverProductFileBackupIfCurrentMissing(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* current,
    const char* backup
);

oc::type::Result<void> recoverPendingProductFileTransaction(
    ProductFileService& files,
    const ProductMutationLease& recoveryLease
);

}  // namespace core::persistence

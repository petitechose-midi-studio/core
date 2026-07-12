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

oc::type::Result<void> removeProductFileIfExists(
    ProductFileService& files,
    const char* path
);

oc::type::Result<void> writeProductFileTemp(
    ProductFileService& files,
    const char* tmpPath,
    const uint8_t* data,
    uint32_t size,
    uint32_t chunkSize
);

oc::type::Result<void> commitProductFileTemp(
    ProductFileService& files,
    const char* current,
    const char* backup,
    const char* tmp
);

oc::type::Result<void> replaceProductFileAtomically(
    ProductFileService& files,
    AtomicProductFilePaths paths,
    const uint8_t* data,
    uint32_t size,
    uint32_t chunkSize
);

oc::type::Result<bool> recoverProductFileBackupIfCurrentMissing(
    ProductFileService& files,
    const char* current,
    const char* backup
);

}  // namespace core::persistence

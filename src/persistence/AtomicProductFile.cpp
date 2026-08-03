#include "persistence/AtomicProductFile.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

FLASHMEM bool isNotFound(const oc::type::Result<void>& result) {
    return !result && result.error().code == ErrorCode::RESOURCE_NOT_FOUND;
}

}  // namespace

FLASHMEM oc::type::Result<void> deleteProductFileIfExists(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path
) {
    auto removed = files.remove(lease, path);
    if (removed || isNotFound(removed)) {
        return oc::type::Result<void>::ok();
    }
    return removed;
}

FLASHMEM oc::type::Result<void> writeProductFileTemp(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* tmpPath,
    const uint8_t* data,
    uint32_t size,
    uint32_t chunkSize
) {
    if (tmpPath == nullptr || data == nullptr || size == 0 || chunkSize == 0) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid atomic file payload"}
        );
    }
    OC_PERF_SCOPE(perfWrite, "persistence.atomic-write");
    OC_PERF_UNITS(perfWrite, size, chunkSize);
    auto begin = files.beginWrite(lease, tmpPath, size);
    if (!begin) return begin;

    uint32_t offset = 0;
    while (offset < size) {
        const uint32_t next = std::min<uint32_t>(chunkSize, size - offset);
        auto written = files.appendWrite(lease, data + offset, next);
        if (!written || written.value() != next) {
            (void)files.abortWrite(lease);
            return oc::type::Result<void>::err(
                {ErrorCode::STORAGE_WRITE_FAILED, "atomic tmp write failed"}
            );
        }
        offset += next;
    }

    auto finish = files.finishWrite(lease);
    if (!finish) {
        (void)files.abortWrite(lease);
        return finish;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> commitProductFileTemp(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* current,
    const char* backup,
    const char* tmp
) {
    if (current == nullptr || backup == nullptr || tmp == nullptr) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid atomic file paths"}
        );
    }

    auto deleteBackup = deleteProductFileIfExists(files, lease, backup);
    if (!deleteBackup) return deleteBackup;

    auto currentInfo = files.stat(lease, current);
    if (!currentInfo && currentInfo.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return oc::type::Result<void>::err(currentInfo.error());
    }
    const bool hadCurrent = static_cast<bool>(currentInfo);
    if (hadCurrent) {
        auto backupResult = files.rename(lease, current, backup);
        if (!backupResult) return backupResult;
    }

    auto promote = files.rename(lease, tmp, current);
    if (!promote) {
        if (hadCurrent) {
            (void)files.rename(lease, backup, current);
        }
        return promote;
    }

    if (hadCurrent) {
        (void)deleteProductFileIfExists(files, lease, backup);
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> replaceProductFileAtomically(
    ProductFileService& files,
    const ProductMutationLease& lease,
    AtomicProductFilePaths paths,
    const uint8_t* data,
    uint32_t size,
    uint32_t chunkSize
) {
    if (paths.directory == nullptr || paths.current == nullptr ||
        paths.backup == nullptr || paths.tmp == nullptr) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid atomic file paths"}
        );
    }

    auto ensureDirectory = files.createDirectory(lease, paths.directory);
    if (!ensureDirectory) return ensureDirectory;

    auto deleteTmp = deleteProductFileIfExists(files, lease, paths.tmp);
    if (!deleteTmp) return deleteTmp;

    auto write = writeProductFileTemp(files, lease, paths.tmp, data, size, chunkSize);
    if (!write) {
        (void)deleteProductFileIfExists(files, lease, paths.tmp);
        return write;
    }

    auto commit = commitProductFileTemp(
        files,
        lease,
        paths.current,
        paths.backup,
        paths.tmp
    );
    if (!commit) {
        (void)deleteProductFileIfExists(files, lease, paths.tmp);
        return commit;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<bool> recoverProductFileBackupIfCurrentMissing(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* current,
    const char* backup
) {
    if (current == nullptr || backup == nullptr) {
        return oc::type::Result<bool>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid recovery paths"}
        );
    }

    auto currentInfo = files.stat(lease, current);
    if (currentInfo) return oc::type::Result<bool>::ok(false);
    if (currentInfo.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return oc::type::Result<bool>::err(currentInfo.error());
    }

    auto backupInfo = files.stat(lease, backup);
    if (!backupInfo) {
        if (backupInfo.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
            return oc::type::Result<bool>::ok(false);
        }
        return oc::type::Result<bool>::err(backupInfo.error());
    }
    if (backupInfo.value().type != oc::interface::FileType::FILE) {
        return oc::type::Result<bool>::err(
            {ErrorCode::INVALID_ARGUMENT, "atomic backup is not a file"}
        );
    }

    auto restored = files.rename(lease, backup, current);
    if (!restored) return oc::type::Result<bool>::err(restored.error());
    return oc::type::Result<bool>::ok(true);
}

}  // namespace core::persistence

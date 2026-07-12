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

FLASHMEM oc::type::Result<void> removeProductFileIfExists(
    ProductFileService& files,
    const char* path
) {
    auto removed = files.remove(path);
    if (removed || isNotFound(removed)) {
        return oc::type::Result<void>::ok();
    }
    return removed;
}

FLASHMEM oc::type::Result<void> writeProductFileTemp(
    ProductFileService& files,
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
    if (files.writeSessionActive()) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_STATE, "write session active"}
        );
    }

    OC_PERF_SCOPE(perfWrite, "persistence.atomic-write");
    OC_PERF_UNITS(perfWrite, size, chunkSize);
    auto begin = files.beginWrite(tmpPath, size);
    if (!begin) return begin;

    uint32_t offset = 0;
    while (offset < size) {
        const uint32_t next = std::min<uint32_t>(chunkSize, size - offset);
        auto written = files.appendWrite(data + offset, next);
        if (!written || written.value() != next) {
            files.abortWrite();
            return oc::type::Result<void>::err(
                {ErrorCode::STORAGE_WRITE_FAILED, "atomic tmp write failed"}
            );
        }
        offset += next;
    }

    auto finish = files.finishWrite();
    if (!finish) {
        files.abortWrite();
        return finish;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> commitProductFileTemp(
    ProductFileService& files,
    const char* current,
    const char* backup,
    const char* tmp
) {
    if (current == nullptr || backup == nullptr || tmp == nullptr) {
        return oc::type::Result<void>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid atomic file paths"}
        );
    }

    auto removeBackup = removeProductFileIfExists(files, backup);
    if (!removeBackup) return removeBackup;

    auto currentInfo = files.stat(current);
    if (!currentInfo && currentInfo.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return oc::type::Result<void>::err(currentInfo.error());
    }
    const bool hadCurrent = static_cast<bool>(currentInfo);
    if (hadCurrent) {
        auto backupResult = files.rename(current, backup);
        if (!backupResult) return backupResult;
    }

    auto promote = files.rename(tmp, current);
    if (!promote) {
        if (hadCurrent) {
            (void)files.rename(backup, current);
        }
        return promote;
    }

    if (hadCurrent) {
        (void)removeProductFileIfExists(files, backup);
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> replaceProductFileAtomically(
    ProductFileService& files,
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

    auto ensureDirectory = files.createDirectory(paths.directory);
    if (!ensureDirectory) return ensureDirectory;

    auto removeTmp = removeProductFileIfExists(files, paths.tmp);
    if (!removeTmp) return removeTmp;

    auto write = writeProductFileTemp(files, paths.tmp, data, size, chunkSize);
    if (!write) {
        (void)removeProductFileIfExists(files, paths.tmp);
        return write;
    }

    auto commit = commitProductFileTemp(files, paths.current, paths.backup, paths.tmp);
    if (!commit) {
        (void)removeProductFileIfExists(files, paths.tmp);
        return commit;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<bool> recoverProductFileBackupIfCurrentMissing(
    ProductFileService& files,
    const char* current,
    const char* backup
) {
    if (current == nullptr || backup == nullptr) {
        return oc::type::Result<bool>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid recovery paths"}
        );
    }

    auto currentInfo = files.stat(current);
    if (currentInfo) return oc::type::Result<bool>::ok(false);
    if (currentInfo.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return oc::type::Result<bool>::err(currentInfo.error());
    }

    auto backupInfo = files.stat(backup);
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

    auto restored = files.rename(backup, current);
    if (!restored) return oc::type::Result<bool>::err(restored.error());
    return oc::type::Result<bool>::ok(true);
}

}  // namespace core::persistence

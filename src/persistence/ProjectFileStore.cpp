#include "persistence/ProjectFileStore.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

FLASHMEM bool pathWrite(char* out,
                        size_t outSize,
                        const char* pattern,
                        const char* projectId) {
    if (out == nullptr || outSize == 0 || pattern == nullptr || projectId == nullptr) {
        return false;
    }
    const int written = std::snprintf(out, outSize, pattern, projectId);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

FLASHMEM bool isProjectIdChar(char c) {
    return (c >= 'A' && c <= 'Z') ||
           (c >= 'a' && c <= 'z') ||
           (c >= '0' && c <= '9') ||
           c == '-' ||
           c == '_';
}

FLASHMEM bool isNotFound(const oc::type::Result<void>& result) {
    return !result && result.error().code == ErrorCode::RESOURCE_NOT_FOUND;
}

}  // namespace

FLASHMEM ProjectFileStore::ProjectFileStore(ProductFileService& files)
    : files_(files) {}

FLASHMEM oc::type::Result<void> ProjectFileStore::invalid_(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, context});
}

FLASHMEM oc::type::Result<void> ProjectFileStore::storageWriteFailed_(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::STORAGE_WRITE_FAILED, context});
}

FLASHMEM oc::type::Result<void> ProjectFileStore::storageReadFailed_(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::STORAGE_READ_FAILED, context});
}

FLASHMEM oc::type::Result<void> ProjectFileStore::resourceExhausted_(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::RESOURCE_EXHAUSTED, context});
}

FLASHMEM bool ProjectFileStore::validProjectId_(const char* projectId) {
    if (projectId == nullptr || projectId[0] == '\0') return false;

    uint8_t length = 0;
    while (projectId[length] != '\0') {
        if (length >= core::state::project::ProjectMetadata::ID_SIZE - 1U) return false;
        if (!isProjectIdChar(projectId[length])) return false;
        ++length;
    }
    return length > 0;
}

FLASHMEM bool ProjectFileStore::buildPaths_(const char* projectId, ProjectPaths& out) {
    if (!validProjectId_(projectId)) return false;
    return pathWrite(out.directory, sizeof(out.directory), "projects/%s", projectId) &&
           pathWrite(out.current, sizeof(out.current), "projects/%s/project.mspj", projectId) &&
           pathWrite(out.backup, sizeof(out.backup), "projects/%s/project.bak", projectId) &&
           pathWrite(out.tmp, sizeof(out.tmp), "tmp/%s.project.tmp", projectId);
}

FLASHMEM oc::type::Result<void> ProjectFileStore::removeIfExists_(const char* path) {
    auto removed = files_.remove(path);
    if (removed || isNotFound(removed)) {
        return oc::type::Result<void>::ok();
    }
    return removed;
}

FLASHMEM oc::type::Result<void> ProjectFileStore::writeTmp_(const char* tmpPath,
                                                            const uint8_t* data,
                                                            uint32_t size) {
    if (data == nullptr || size == 0) {
        return invalid_("empty project payload");
    }
    if (files_.writeSessionActive()) {
        return oc::type::Result<void>::err({ErrorCode::INVALID_STATE, "write session active"});
    }

    auto begin = files_.beginWrite(tmpPath, size);
    if (!begin) return begin;

    uint32_t offset = 0;
    while (offset < size) {
        const uint32_t next = std::min<uint32_t>(WRITE_CHUNK_SIZE, size - offset);
        auto written = files_.appendWrite(data + offset, next);
        if (!written || written.value() != next) {
            files_.abortWrite();
            return storageWriteFailed_("project tmp write failed");
        }
        offset += next;
    }

    auto finish = files_.finishWrite();
    if (!finish) {
        files_.abortWrite();
        return finish;
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<void> ProjectFileStore::commitTmp_(const ProjectPaths& paths) {
    auto removeBackup = removeIfExists_(paths.backup);
    if (!removeBackup) return removeBackup;

    auto currentInfo = files_.stat(paths.current);
    if (!currentInfo && currentInfo.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return oc::type::Result<void>::err(currentInfo.error());
    }
    const bool hadCurrent = static_cast<bool>(currentInfo);
    if (hadCurrent) {
        auto backup = files_.rename(paths.current, paths.backup);
        if (!backup) {
            return backup;
        }
    }

    auto promote = files_.rename(paths.tmp, paths.current);
    if (!promote) {
        if (hadCurrent) {
            (void)files_.rename(paths.backup, paths.current);
        }
        return promote;
    }

    if (hadCurrent) {
        (void)removeIfExists_(paths.backup);
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<ProjectSaveResult> ProjectFileStore::save(
    const core::state::project::ProjectSnapshot& snapshot
) {
    ProjectPaths paths{};
    if (!buildPaths_(snapshot.project.metadata.id.data(), paths)) {
        return oc::type::Result<ProjectSaveResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid project id"}
        );
    }

    auto ensureDir = files_.createDirectory(paths.directory);
    if (!ensureDir) {
        return oc::type::Result<ProjectSaveResult>::err(ensureDir.error());
    }

    auto removeTmp = removeIfExists_(paths.tmp);
    if (!removeTmp) {
        return oc::type::Result<ProjectSaveResult>::err(removeTmp.error());
    }

    using Buffer = std::array<uint8_t, MAX_PROJECT_FILE_SIZE>;
    auto buffer = core::app::makeExtmemUnique<Buffer>();
    if (!buffer) {
        return oc::type::Result<ProjectSaveResult>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "project save buffer"}
        );
    }

    auto encoded = core::persistence::project_snapshot_codec::encodeProjectSnapshot(
        snapshot,
        buffer->data(),
        static_cast<uint32_t>(buffer->size())
    );
    if (encoded.status != core::persistence::project_file::Status::OK) {
        return oc::type::Result<ProjectSaveResult>::err(
            {ErrorCode::STORAGE_WRITE_FAILED, "project encode failed"}
        );
    }

    auto write = writeTmp_(paths.tmp, buffer->data(), encoded.bytesWritten);
    if (!write) {
        (void)removeIfExists_(paths.tmp);
        return oc::type::Result<ProjectSaveResult>::err(write.error());
    }

    auto commit = commitTmp_(paths);
    if (!commit) {
        (void)removeIfExists_(paths.tmp);
        return oc::type::Result<ProjectSaveResult>::err(commit.error());
    }

    ProjectSaveResult result{};
    result.bytesWritten = encoded.bytesWritten;
    std::strncpy(result.projectPath, paths.current, sizeof(result.projectPath) - 1U);
    return oc::type::Result<ProjectSaveResult>::ok(result);
}

FLASHMEM oc::type::Result<ProjectLoadResult> ProjectFileStore::load(
    const char* projectId,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    ProjectPaths paths{};
    if (!buildPaths_(projectId, paths)) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid project id"}
        );
    }

    auto info = files_.stat(paths.current);
    if (!info) {
        return oc::type::Result<ProjectLoadResult>::err(info.error());
    }
    if (info.value().type != oc::interface::FileType::FILE) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "project path is not a file"}
        );
    }
    if (info.value().sizeBytes == 0 || info.value().sizeBytes > MAX_PROJECT_FILE_SIZE) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "project file too large"}
        );
    }

    using Buffer = std::array<uint8_t, MAX_PROJECT_FILE_SIZE>;
    auto buffer = core::app::makeExtmemUnique<Buffer>();
    if (!buffer) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "project load buffer"}
        );
    }

    auto read = files_.read(paths.current, 0, buffer->data(), info.value().sizeBytes);
    if (!read) {
        return oc::type::Result<ProjectLoadResult>::err(read.error());
    }
    if (read.value() != info.value().sizeBytes) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::STORAGE_READ_FAILED, "short project read"}
        );
    }

    auto decoded = core::persistence::project_snapshot_codec::decodeProjectSnapshot(
        buffer->data(),
        info.value().sizeBytes,
        out,
        report
    );
    if (!decoded.ok) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::STORAGE_CORRUPT, "project decode failed"}
        );
    }

    ProjectLoadResult result{};
    result.bytesRead = static_cast<uint32_t>(read.value());
    result.loadStatus = decoded.loadStatus;
    result.overwriteSafe = decoded.overwriteSafe;
    std::strncpy(result.projectPath, paths.current, sizeof(result.projectPath) - 1U);
    return oc::type::Result<ProjectLoadResult>::ok(result);
}

}  // namespace core::persistence

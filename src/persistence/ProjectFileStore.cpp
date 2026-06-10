#include "persistence/ProjectFileStore.hpp"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "persistence/ProjectSessionStore.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"
#include "state/project/ProjectSlug.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

struct ProjectListContext {
    ProjectFileStore* store = nullptr;
    ProjectListEntry* entries = nullptr;
    uint8_t capacity = 0;
    ProjectListResult result{};
};

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

FLASHMEM bool pathWriteLiteral(char* out, size_t outSize, const char* path) {
    if (out == nullptr || outSize == 0 || path == nullptr) return false;
    const size_t length = std::strlen(path);
    if (length >= outSize) return false;
    std::memcpy(out, path, length);
    out[length] = '\0';
    return true;
}

FLASHMEM bool isNotFound(const oc::type::Result<void>& result) {
    return !result && result.error().code == ErrorCode::RESOURCE_NOT_FOUND;
}

FLASHMEM oc::type::Result<void> invalid(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::INVALID_ARGUMENT, context});
}

FLASHMEM oc::type::Result<void> storageWriteFailed(const char* context) {
    return oc::type::Result<void>::err({ErrorCode::STORAGE_WRITE_FAILED, context});
}

FLASHMEM oc::type::Result<void> removeIfExists(ProductFileService& files, const char* path) {
    auto removed = files.remove(path);
    if (removed || isNotFound(removed)) {
        return oc::type::Result<void>::ok();
    }
    return removed;
}

FLASHMEM oc::type::Result<void> writeTmp(ProductFileService& files,
                                         const char* tmpPath,
                                         const uint8_t* data,
                                         uint32_t size) {
    if (data == nullptr || size == 0) {
        return invalid("empty project payload");
    }
    if (files.writeSessionActive()) {
        return oc::type::Result<void>::err({ErrorCode::INVALID_STATE, "write session active"});
    }

    auto begin = files.beginWrite(tmpPath, size);
    if (!begin) return begin;

    uint32_t offset = 0;
    while (offset < size) {
        const uint32_t next = std::min<uint32_t>(
            ProjectFileStore::WRITE_CHUNK_SIZE,
            size - offset
        );
        auto written = files.appendWrite(data + offset, next);
        if (!written || written.value() != next) {
            files.abortWrite();
            return storageWriteFailed("project tmp write failed");
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

FLASHMEM oc::type::Result<void> commitTmp(ProductFileService& files,
                                          const char* current,
                                          const char* backup,
                                          const char* tmp) {
    auto removeBackup = removeIfExists(files, backup);
    if (!removeBackup) return removeBackup;

    auto currentInfo = files.stat(current);
    if (!currentInfo && currentInfo.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
        return oc::type::Result<void>::err(currentInfo.error());
    }
    const bool hadCurrent = static_cast<bool>(currentInfo);
    if (hadCurrent) {
        auto backupResult = files.rename(current, backup);
        if (!backupResult) {
            return backupResult;
        }
    }

    auto promote = files.rename(tmp, current);
    if (!promote) {
        if (hadCurrent) {
            (void)files.rename(backup, current);
        }
        return promote;
    }

    if (hadCurrent) {
        (void)removeIfExists(files, backup);
    }
    return oc::type::Result<void>::ok();
}

FLASHMEM oc::type::Result<ProjectSaveResult> saveSnapshot(ProductFileService& files,
                                                          const core::state::project::ProjectSnapshot& snapshot,
                                                          const char* directory,
                                                          const char* current,
                                                          const char* backup,
                                                          const char* tmp) {
    auto ensureDir = files.createDirectory(directory);
    if (!ensureDir) {
        return oc::type::Result<ProjectSaveResult>::err(ensureDir.error());
    }

    auto removeTmp = removeIfExists(files, tmp);
    if (!removeTmp) {
        return oc::type::Result<ProjectSaveResult>::err(removeTmp.error());
    }

    using Buffer = std::array<uint8_t, ProjectFileStore::MAX_PROJECT_FILE_SIZE>;
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

    auto write = writeTmp(files, tmp, buffer->data(), encoded.bytesWritten);
    if (!write) {
        (void)removeIfExists(files, tmp);
        return oc::type::Result<ProjectSaveResult>::err(write.error());
    }

    auto commit = commitTmp(files, current, backup, tmp);
    if (!commit) {
        (void)removeIfExists(files, tmp);
        return oc::type::Result<ProjectSaveResult>::err(commit.error());
    }

    ProjectSaveResult result{};
    result.bytesWritten = encoded.bytesWritten;
    std::strncpy(result.projectPath, current, sizeof(result.projectPath) - 1U);
    return oc::type::Result<ProjectSaveResult>::ok(result);
}

FLASHMEM oc::type::Result<ProjectLoadResult> loadSnapshotFromPath(
    ProductFileService& files,
    const char* path,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    auto info = files.stat(path);
    if (!info) {
        return oc::type::Result<ProjectLoadResult>::err(info.error());
    }
    if (info.value().type != oc::interface::FileType::FILE) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "project path is not a file"}
        );
    }
    if (info.value().sizeBytes == 0 ||
        info.value().sizeBytes > ProjectFileStore::MAX_PROJECT_FILE_SIZE) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "project file too large"}
        );
    }

    using Buffer = std::array<uint8_t, ProjectFileStore::MAX_PROJECT_FILE_SIZE>;
    auto buffer = core::app::makeExtmemUnique<Buffer>();
    if (!buffer) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "project load buffer"}
        );
    }

    auto read = files.read(path, 0, buffer->data(), info.value().sizeBytes);
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
    std::strncpy(result.projectPath, path, sizeof(result.projectPath) - 1U);
    return oc::type::Result<ProjectLoadResult>::ok(result);
}

FLASHMEM bool shouldTryBackup(const oc::type::Result<ProjectLoadResult>& result) {
    if (result) return false;
    const auto code = result.error().code;
    return code == ErrorCode::RESOURCE_NOT_FOUND || code == ErrorCode::STORAGE_CORRUPT;
}

FLASHMEM void restoreBackupAsCurrent(ProductFileService& files,
                                     const char* current,
                                     const char* backup,
                                     ProjectLoadResult& result) {
    (void)removeIfExists(files, current);
    auto restored = files.rename(backup, current);
    if (!restored) return;

    std::strncpy(result.projectPath, current, sizeof(result.projectPath) - 1U);
    result.projectPath[sizeof(result.projectPath) - 1U] = '\0';
}

FLASHMEM oc::type::Result<ProjectLoadResult> loadSnapshot(
    ProductFileService& files,
    const char* current,
    const char* backup,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    auto loaded = loadSnapshotFromPath(files, current, out, report);
    if (!shouldTryBackup(loaded)) {
        return loaded;
    }

    auto backupLoaded = loadSnapshotFromPath(files, backup, out, report);
    if (!backupLoaded) {
        return loaded;
    }

    auto result = backupLoaded.value();
    restoreBackupAsCurrent(files, current, backup, result);
    return oc::type::Result<ProjectLoadResult>::ok(result);
}

}  // namespace

FLASHMEM ProjectFileStore::ProjectFileStore(ProductFileService& files)
    : files_(files) {}

FLASHMEM bool ProjectFileStore::listProjectsVisitor_(
    const oc::interface::DirectoryEntry& entry,
    void* context
) {
    auto* list = static_cast<ProjectListContext*>(context);
    if (!list || !list->store || !list->entries) return false;
    if (entry.type != oc::interface::FileType::FILE || entry.nameTruncated) return true;

    constexpr const char* extension = core::state::project::PROJECT_FILE_EXTENSION;
    constexpr size_t extensionLength = core::state::project::PROJECT_FILE_EXTENSION_LENGTH;
    const size_t nameLength = std::strlen(entry.name);
    if (nameLength <= extensionLength) return true;
    if (std::strcmp(entry.name + nameLength - extensionLength, extension) != 0) return true;

    char projectId[core::state::project::ProjectMetadata::ID_SIZE] = {};
    const size_t slugLength = nameLength - extensionLength;
    if (slugLength >= sizeof(projectId)) return true;
    std::memcpy(projectId, entry.name, slugLength);
    projectId[slugLength] = '\0';
    if (!validProjectId_(projectId)) return true;

    ProjectPaths paths{};
    if (!buildPaths_(projectId, paths)) return true;

    auto info = list->store->files_.stat(paths.current);
    if (!info || info.value().type != oc::interface::FileType::FILE) return true;
    if (info.value().sizeBytes == 0 || info.value().sizeBytes > MAX_PROJECT_FILE_SIZE) {
        return true;
    }

    if (list->result.count >= list->capacity) {
        list->result.truncated = true;
        return true;
    }

    auto& target = list->entries[list->result.count++];
    std::strncpy(target.id, projectId, sizeof(target.id) - 1U);
    target.id[sizeof(target.id) - 1U] = '\0';
    target.sizeBytes = info.value().sizeBytes;
    return true;
}

FLASHMEM bool ProjectFileStore::validProjectId_(const char* projectId) {
    return core::state::project::validProjectSlug(projectId);
}

FLASHMEM bool ProjectFileStore::buildPaths_(const char* projectId, ProjectPaths& out) {
    if (!validProjectId_(projectId)) return false;
    return pathWriteLiteral(out.directory, sizeof(out.directory), "projects") &&
           pathWrite(out.current, sizeof(out.current), "projects/%s.mspj", projectId) &&
           pathWrite(out.backup,
                     sizeof(out.backup),
                     "projects/%s.mspj.bak",
                     projectId) &&
           pathWrite(out.tmp, sizeof(out.tmp), "tmp/%s.mspj.tmp", projectId);
}

FLASHMEM oc::type::Result<ProjectSaveResult> ProjectFileStore::save(
    const core::state::project::ProjectSnapshot& snapshot
) {
    if (std::strcmp(
            snapshot.project.metadata.id.data(),
            snapshot.project.metadata.name.data()
        ) != 0) {
        return oc::type::Result<ProjectSaveResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "project slug mismatch"}
        );
    }

    ProjectPaths paths{};
    if (!buildPaths_(snapshot.project.metadata.id.data(), paths)) {
        return oc::type::Result<ProjectSaveResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid project id"}
        );
    }

    return saveSnapshot(files_, snapshot, paths.directory, paths.current, paths.backup, paths.tmp);
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

    return loadSnapshot(files_, paths.current, paths.backup, out, report);
}

FLASHMEM oc::type::Result<ProjectListResult> ProjectFileStore::listProjects(
    ProjectListEntry* entries,
    uint8_t capacity
) {
    if (entries == nullptr && capacity > 0) {
        return oc::type::Result<ProjectListResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "invalid project list buffer"}
        );
    }
    if (capacity == 0) {
        return oc::type::Result<ProjectListResult>::ok(ProjectListResult{});
    }

    for (uint8_t i = 0; i < capacity; ++i) {
        entries[i] = ProjectListEntry{};
    }

    ProjectListContext context{this, entries, capacity, ProjectListResult{}};
    auto listed = files_.list("projects", listProjectsVisitor_, &context);
    if (!listed) {
        return oc::type::Result<ProjectListResult>::err(listed.error());
    }
    for (uint8_t i = 1; i < context.result.count; ++i) {
        ProjectListEntry current = entries[i];
        uint8_t insert = i;
        while (insert > 0 && std::strcmp(entries[insert - 1U].id, current.id) > 0) {
            entries[insert] = entries[insert - 1U];
            --insert;
        }
        entries[insert] = current;
    }
    return oc::type::Result<ProjectListResult>::ok(context.result);
}

FLASHMEM ProjectSessionStore::ProjectSessionStore(ProductFileService& files)
    : files_(files) {}

FLASHMEM oc::type::Result<ProjectSaveResult> ProjectSessionStore::saveCurrent(
    const core::state::project::ProjectSnapshot& snapshot
) {
    return saveSnapshot(
        files_,
        snapshot,
        "session",
        CURRENT_SESSION_PATH,
        CURRENT_SESSION_BACKUP_PATH,
        CURRENT_SESSION_TMP_PATH
    );
}

FLASHMEM oc::type::Result<ProjectLoadResult> ProjectSessionStore::loadCurrent(
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    return loadSnapshot(files_, CURRENT_SESSION_PATH, CURRENT_SESSION_BACKUP_PATH, out, report);
}

}  // namespace core::persistence

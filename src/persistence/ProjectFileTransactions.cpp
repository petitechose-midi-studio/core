#include "persistence/ProjectFileTransactions.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

namespace core::persistence::project_file_transactions {

namespace {

using oc::type::ErrorCode;

FLASHMEM oc::type::Result<ProjectLoadResult> loadFromPath(
    ProductFileService& files,
    ProjectFileWorkspace& workspace,
    const char* path,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    auto info = files.stat(path);
    if (!info) return oc::type::Result<ProjectLoadResult>::err(info.error());
    if (info.value().type != oc::interface::FileType::FILE) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::INVALID_ARGUMENT, "project path is not a file"}
        );
    }
    if (info.value().sizeBytes == 0 || info.value().sizeBytes > PROJECT_FILE_MAX_SIZE) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "project file too large"}
        );
    }
    if (!workspace.prepare()) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "project load buffer"}
        );
    }

    auto read = files.read(path, 0, workspace.data(), info.value().sizeBytes);
    if (!read) return oc::type::Result<ProjectLoadResult>::err(read.error());
    if (read.value() != info.value().sizeBytes) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::STORAGE_READ_FAILED, "short project read"}
        );
    }

    auto decoded = project_snapshot_codec::decodeProjectSnapshot(
        workspace.data(), info.value().sizeBytes, out, report
    );
    if (!decoded.ok) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::STORAGE_CORRUPT, "project decode failed"}
        );
    }

    ProjectLoadResult result{};
    result.bytesRead = static_cast<uint32_t>(read.value());
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
    auto deleted = deleteProductFileIfExists(files, current);
    if (!deleted) return;
    auto restored = files.rename(backup, current);
    if (!restored) return;

    std::strncpy(result.projectPath, current, sizeof(result.projectPath) - 1U);
    result.projectPath[sizeof(result.projectPath) - 1U] = '\0';
}

FLASHMEM void copyReport(core::persistence::project_file::LoadReport* target,
                         const core::persistence::project_file::LoadReport& source) {
    if (target != nullptr) *target = source;
}

}  // namespace

FLASHMEM oc::type::Result<ProjectSaveResult> saveToCompletion(
    ProjectSaveTransaction& transaction,
    const core::state::project::ProjectSnapshot& snapshot,
    AtomicProductFilePaths paths
) {
    auto started = transaction.begin(snapshot, paths);
    if (!started) return oc::type::Result<ProjectSaveResult>::err(started.error());

    while (transaction.active()) {
        auto progress = transaction.advance();
        if (!progress) return oc::type::Result<ProjectSaveResult>::err(progress.error());
        if (!progress.value().complete) continue;

        ProjectSaveResult result{};
        result.bytesWritten = progress.value().bytesWritten;
        std::strncpy(result.projectPath, paths.current, sizeof(result.projectPath) - 1U);
        return oc::type::Result<ProjectSaveResult>::ok(result);
    }

    return oc::type::Result<ProjectSaveResult>::err(
        {ErrorCode::INVALID_STATE, "project save stopped before commit"}
    );
}

FLASHMEM oc::type::Result<ProjectLoadResult> loadWithBackup(
    ProductFileService& files,
    ProjectFileWorkspace& workspace,
    const char* current,
    const char* backup,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    core::persistence::project_file::LoadReport currentReport{};
    auto loaded = loadFromPath(files, workspace, current, out, &currentReport);
    if (!shouldTryBackup(loaded)) {
        copyReport(report, currentReport);
        return loaded;
    }

    core::persistence::project_file::LoadReport backupReport{};
    auto backupLoaded = loadFromPath(files, workspace, backup, out, &backupReport);
    if (!backupLoaded) {
        const bool currentMissing = loaded.error().code == ErrorCode::RESOURCE_NOT_FOUND;
        const bool backupMissing =
            backupLoaded.error().code == ErrorCode::RESOURCE_NOT_FOUND;
        if (currentMissing && !backupMissing) {
            copyReport(report, backupReport);
            return backupLoaded;
        }
        copyReport(report, currentReport);
        return loaded;
    }

    copyReport(report, backupReport);
    auto result = backupLoaded.value();
    restoreBackupAsCurrent(files, current, backup, result);
    return oc::type::Result<ProjectLoadResult>::ok(result);
}

}  // namespace core::persistence::project_file_transactions

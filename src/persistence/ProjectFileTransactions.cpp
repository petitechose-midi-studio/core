#include "persistence/ProjectFileTransactions.hpp"

#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectSnapshotPersistenceCodec.hpp"

namespace core::persistence::project_file_transactions {

namespace {

using oc::type::ErrorCode;

FLASHMEM oc::type::Result<ProjectLoadResult> loadFromPath(
    ProductFileService& files,
    const ProductMutationLease& lease,
    ProjectFileWorkspace& workspace,
    const char* path,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    uint32_t fileSize = 0;
    {
        auto info = files.stat(lease, path);
        if (!info) return oc::type::Result<ProjectLoadResult>::err(info.error());
        if (info.value().type != oc::interface::FileType::FILE) {
            return oc::type::Result<ProjectLoadResult>::err(
                {ErrorCode::INVALID_ARGUMENT, "project path is not a file"}
            );
        }
        if (info.value().sizeBytes == 0 ||
            info.value().sizeBytes > PROJECT_FILE_MAX_SIZE) {
            return oc::type::Result<ProjectLoadResult>::err(
                {ErrorCode::RESOURCE_EXHAUSTED, "project file too large"}
            );
        }
        fileSize = info.value().sizeBytes;
    }
    if (!workspace.prepare()) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::RESOURCE_EXHAUSTED, "project load buffer"}
        );
    }

    uint32_t bytesRead = 0;
    {
        auto read = files.read(lease, path, 0, workspace.data(), fileSize);
        if (!read) return oc::type::Result<ProjectLoadResult>::err(read.error());
        if (read.value() != fileSize) {
            return oc::type::Result<ProjectLoadResult>::err(
                {ErrorCode::STORAGE_READ_FAILED, "short project read"}
            );
        }
        bytesRead = static_cast<uint32_t>(read.value());
    }

    auto decoded = project_snapshot_codec::decodeProjectSnapshot(
        workspace.data(), fileSize, out, report
    );
    if (!decoded.ok) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::STORAGE_CORRUPT, "project decode failed"}
        );
    }

    ProjectLoadResult result{};
    result.bytesRead = bytesRead;
    std::strncpy(result.projectPath, path, sizeof(result.projectPath) - 1U);
    return oc::type::Result<ProjectLoadResult>::ok(result);
}

FLASHMEM bool shouldTryBackup(const oc::type::Result<ProjectLoadResult>& result) {
    if (result) return false;
    const auto code = result.error().code;
    return code == ErrorCode::RESOURCE_NOT_FOUND || code == ErrorCode::STORAGE_CORRUPT;
}

FLASHMEM void restoreBackupAsCurrent(ProductFileService& files,
                                     const ProductMutationLease& lease,
                                     const char* current,
                                     const char* backup,
                                     ProjectLoadResult& result) {
    auto deleted = deleteProductFileIfExists(files, lease, current);
    if (!deleted) return;
    auto restored = files.rename(lease, backup, current);
    if (!restored) return;

    std::strncpy(result.projectPath, current, sizeof(result.projectPath) - 1U);
    result.projectPath[sizeof(result.projectPath) - 1U] = '\0';
}

FLASHMEM void copyReport(core::persistence::project_file::LoadReport* target,
                         const core::persistence::project_file::LoadReport& source) {
    if (target != nullptr) *target = source;
}

FLASHMEM oc::type::Result<ProjectLoadResult> loadWithBackupUsingLease(
    ProductFileService& files,
    const ProductMutationLease& lease,
    ProjectFileWorkspace& workspace,
    const char* current,
    const char* backup,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    // Preserve the first attempt in the caller, then reuse one bounded report
    // for the backup. Retaining both reports inflated every Project load stack
    // even though the two decodes are strictly sequential.
    core::persistence::project_file::LoadReport attemptReport{};
    auto loaded = loadFromPath(files, lease, workspace, current, out, &attemptReport);
    if (!shouldTryBackup(loaded)) {
        copyReport(report, attemptReport);
        return loaded;
    }

    copyReport(report, attemptReport);
    attemptReport = {};
    auto backupLoaded = loadFromPath(files, lease, workspace, backup, out, &attemptReport);
    if (!backupLoaded) {
        const bool currentMissing = loaded.error().code == ErrorCode::RESOURCE_NOT_FOUND;
        const bool backupMissing =
            backupLoaded.error().code == ErrorCode::RESOURCE_NOT_FOUND;
        if (currentMissing && !backupMissing) {
            copyReport(report, attemptReport);
            return backupLoaded;
        }
        return loaded;
    }

    copyReport(report, attemptReport);
    auto result = backupLoaded.value();
    restoreBackupAsCurrent(files, lease, current, backup, result);
    return oc::type::Result<ProjectLoadResult>::ok(result);
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

FLASHMEM oc::type::Result<ProjectSaveResult> saveToCompletionWithRecoveryLease(
    ProjectSaveTransaction& transaction,
    const core::state::project::ProjectSnapshot& snapshot,
    AtomicProductFilePaths paths,
    const ProductMutationLease& recoveryLease
) {
    auto progress = transaction.saveToCompletionWithRecoveryLease(
        snapshot,
        paths,
        recoveryLease
    );
    if (!progress) {
        return oc::type::Result<ProjectSaveResult>::err(progress.error());
    }
    if (!progress.value().complete) {
        return oc::type::Result<ProjectSaveResult>::err(
            {ErrorCode::INVALID_STATE, "project recovery save stopped before commit"}
        );
    }

    ProjectSaveResult result{};
    result.bytesWritten = progress.value().bytesWritten;
    std::strncpy(result.projectPath, paths.current, sizeof(result.projectPath) - 1U);
    return oc::type::Result<ProjectSaveResult>::ok(result);
}

FLASHMEM oc::type::Result<ProjectLoadResult> loadWithBackup(
    ProductFileService& files,
    ProjectFileWorkspace& workspace,
    const char* current,
    const char* backup,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    auto acquired = files.acquireMutation(ProductMutationOwner::PROJECT);
    if (!acquired) {
        return oc::type::Result<ProjectLoadResult>::err(acquired.error());
    }
    auto lease = std::move(acquired.value());
    auto result = loadWithBackupUsingLease(
        files,
        lease,
        workspace,
        current,
        backup,
        out,
        report
    );
    auto released = files.releaseMutation(lease);
    if (!released) return oc::type::Result<ProjectLoadResult>::err(released.error());
    return result;
}

FLASHMEM oc::type::Result<ProjectLoadResult> loadWithBackup(
    ProductFileService& files,
    const ProductMutationLease& recoveryLease,
    ProjectFileWorkspace& workspace,
    const char* current,
    const char* backup,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    if (!files.owns(recoveryLease, ProductMutationOwner::RECOVERY)) {
        return oc::type::Result<ProjectLoadResult>::err(
            {ErrorCode::INVALID_STATE, "exact recovery lease required"}
        );
    }
    return loadWithBackupUsingLease(
        files,
        recoveryLease,
        workspace,
        current,
        backup,
        out,
        report
    );
}

}  // namespace core::persistence::project_file_transactions

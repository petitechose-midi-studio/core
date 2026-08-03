#include "persistence/ProjectSessionStore.hpp"

#include <config/PlatformCompat.hpp>

#include "persistence/ProjectFileTransactions.hpp"

namespace core::persistence {

FLASHMEM ProjectSessionStore::ProjectSessionStore(ProductFileService& files)
    : files_(files), save_transaction_(files_, workspace_) {}

FLASHMEM bool ProjectSessionStore::prepareWorkspace() {
    return workspace_.prepare();
}

FLASHMEM oc::type::Result<ProjectSaveResult> ProjectSessionStore::saveCurrent(
    const core::state::project::ProjectSnapshot& snapshot
) {
    return project_file_transactions::saveToCompletion(
        save_transaction_,
        snapshot,
        {
            .directory = "session",
            .current = CURRENT_SESSION_PATH,
            .backup = CURRENT_SESSION_BACKUP_PATH,
            .tmp = CURRENT_SESSION_TMP_PATH,
        }
    );
}

FLASHMEM oc::type::Result<ProjectSaveResult> ProjectSessionStore::saveCurrent(
    const core::state::project::ProjectSnapshot& snapshot,
    const ProductMutationLease& recoveryLease
) {
    return project_file_transactions::saveToCompletionWithRecoveryLease(
        save_transaction_,
        snapshot,
        {
            .directory = "session",
            .current = CURRENT_SESSION_PATH,
            .backup = CURRENT_SESSION_BACKUP_PATH,
            .tmp = CURRENT_SESSION_TMP_PATH,
        },
        recoveryLease
    );
}

FLASHMEM oc::type::Result<void> ProjectSessionStore::beginSaveCurrent(
    const core::state::project::ProjectSnapshot& snapshot
) {
    return save_transaction_.begin(
        snapshot,
        {
            .directory = "session",
            .current = CURRENT_SESSION_PATH,
            .backup = CURRENT_SESSION_BACKUP_PATH,
            .tmp = CURRENT_SESSION_TMP_PATH,
        }
    );
}

FLASHMEM oc::type::Result<ProjectSaveProgress> ProjectSessionStore::advanceSaveCurrent() {
    return save_transaction_.advance();
}

FLASHMEM void ProjectSessionStore::cancelSaveCurrent() {
    save_transaction_.cancel();
}

bool ProjectSessionStore::saveCurrentInProgress() const {
    return save_transaction_.active();
}

bool ProjectSessionStore::saveCurrentWriteSessionActive() const {
    return save_transaction_.writeSessionActive();
}

FLASHMEM oc::type::Result<ProjectLoadResult> ProjectSessionStore::loadCurrent(
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
) {
    if (save_transaction_.active()) {
        return oc::type::Result<ProjectLoadResult>::err(
            {oc::type::ErrorCode::INVALID_STATE, "project session save active"}
        );
    }
    return project_file_transactions::loadWithBackup(
        files_, workspace_, CURRENT_SESSION_PATH, CURRENT_SESSION_BACKUP_PATH, out, report
    );
}

FLASHMEM oc::type::Result<ProjectLoadResult> ProjectSessionStore::loadCurrent(
    core::state::project::ProjectSnapshot& out,
    const ProductMutationLease& recoveryLease,
    core::persistence::project_file::LoadReport* report
) {
    if (save_transaction_.active()) {
        return oc::type::Result<ProjectLoadResult>::err(
            {oc::type::ErrorCode::INVALID_STATE, "project session save active"}
        );
    }
    return project_file_transactions::loadWithBackup(
        files_,
        recoveryLease,
        workspace_,
        CURRENT_SESSION_PATH,
        CURRENT_SESSION_BACKUP_PATH,
        out,
        report
    );
}

}  // namespace core::persistence

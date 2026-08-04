#pragma once

#include "persistence/ProjectFileStore.hpp"
#include "persistence/ProjectSaveTransaction.hpp"

namespace core::persistence::project_file_transactions {

oc::type::Result<ProjectSaveResult> saveToCompletion(
    ProjectSaveTransaction& transaction,
    const core::state::project::ProjectSnapshot& snapshot,
    AtomicProductFilePaths paths
);

oc::type::Result<ProjectSaveResult> saveToCompletionWithRecoveryLease(
    ProjectSaveTransaction& transaction,
    const core::state::project::ProjectSnapshot& snapshot,
    AtomicProductFilePaths paths,
    const ProductMutationLease& recoveryLease,
    ProjectSaveStage* failedStage = nullptr
);

oc::type::Result<ProjectLoadResult> loadWithBackup(
    ProductFileService& files,
    const char* current,
    const char* backup,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
);

oc::type::Result<ProjectLoadResult> loadWithBackup(
    ProductFileService& files,
    const ProductMutationLease& recoveryLease,
    const char* current,
    const char* backup,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
);

}  // namespace core::persistence::project_file_transactions

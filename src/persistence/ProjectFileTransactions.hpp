#pragma once

#include "persistence/ProjectFileStore.hpp"
#include "persistence/ProjectSaveTransaction.hpp"

namespace core::persistence::project_file_transactions {

oc::type::Result<ProjectSaveResult> saveToCompletion(
    ProjectSaveTransaction& transaction,
    const core::state::project::ProjectSnapshot& snapshot,
    AtomicProductFilePaths paths
);

oc::type::Result<ProjectLoadResult> loadWithBackup(
    ProductFileService& files,
    ProjectFileWorkspace& workspace,
    const char* current,
    const char* backup,
    core::state::project::ProjectSnapshot& out,
    core::persistence::project_file::LoadReport* report
);

}  // namespace core::persistence::project_file_transactions

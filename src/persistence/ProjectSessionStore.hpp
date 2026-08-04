#pragma once

#include "persistence/ProjectFileStore.hpp"
#include "persistence/ProjectSaveTransaction.hpp"

namespace core::persistence {

class ProjectSessionStore {
public:
    static constexpr const char* CURRENT_SESSION_PATH = "session/current.mspj";
    static constexpr const char* CURRENT_SESSION_BACKUP_PATH = "session/current.bak";
    static constexpr const char* CURRENT_SESSION_TMP_PATH = "tmp/session.current.tmp";

    explicit ProjectSessionStore(ProductFileService& files);
    ProjectSessionStore(const ProjectSessionStore&) = delete;
    ProjectSessionStore& operator=(const ProjectSessionStore&) = delete;
    ProjectSessionStore(ProjectSessionStore&&) = delete;
    ProjectSessionStore& operator=(ProjectSessionStore&&) = delete;

    bool prepareWorkspace();
    ProductFileService& productFiles() { return files_; }
    const ProductFileService& productFiles() const { return files_; }

    oc::type::Result<ProjectSaveResult> saveCurrent(
        const core::state::project::ProjectSnapshot& snapshot
    );
    oc::type::Result<ProjectSaveResult> saveCurrent(
        const core::state::project::ProjectSnapshot& snapshot,
        const ProductMutationLease& recoveryLease,
        ProjectSaveStage* failedStage = nullptr
    );
    oc::type::Result<void> beginSaveCurrent(
        const core::state::project::ProjectSnapshot& snapshot
    );
    oc::type::Result<void> beginSaveCurrent(
        const core::state::project::ProjectSnapshot& snapshot,
        const ProductMutationLease& recoveryLease
    );
    oc::type::Result<ProjectSaveProgress> advanceSaveCurrent(
        ProjectSaveStage* attemptedStage = nullptr
    );
    void cancelSaveCurrent();
    bool saveCurrentInProgress() const;
    bool saveCurrentWriteSessionActive() const;
    ProjectSaveStage saveCurrentStage() const;

    oc::type::Result<ProjectLoadResult> loadCurrent(
        core::state::project::ProjectSnapshot& out,
        core::persistence::project_file::LoadReport* report = nullptr
    );
    oc::type::Result<ProjectLoadResult> loadCurrent(
        core::state::project::ProjectSnapshot& out,
        const ProductMutationLease& recoveryLease,
        core::persistence::project_file::LoadReport* report = nullptr
    );

private:
    ProductFileService& files_;
    ProjectFileWriteWorkspace workspace_;
    ProjectSaveTransaction save_transaction_;
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(ProjectSessionStore) == 64U, "project session store ABI drift");
static_assert(alignof(ProjectSessionStore) == 4U, "project session store alignment drift");
#endif

}  // namespace core::persistence

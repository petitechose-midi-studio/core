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

    oc::type::Result<ProjectSaveResult> saveCurrent(
        const core::state::project::ProjectSnapshot& snapshot
    );
    oc::type::Result<void> beginSaveCurrent(
        const core::state::project::ProjectSnapshot& snapshot
    );
    oc::type::Result<ProjectSaveProgress> advanceSaveCurrent();
    void cancelSaveCurrent();
    bool saveCurrentInProgress() const;
    bool saveCurrentWriteSessionActive() const;

    oc::type::Result<ProjectLoadResult> loadCurrent(
        core::state::project::ProjectSnapshot& out,
        core::persistence::project_file::LoadReport* report = nullptr
    );

private:
    ProductFileService& files_;
    ProjectFileWorkspace workspace_;
    ProjectSaveTransaction save_transaction_;
};

}  // namespace core::persistence

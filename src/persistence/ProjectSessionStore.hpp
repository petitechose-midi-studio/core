#pragma once

#include "persistence/ProjectFileStore.hpp"

namespace core::persistence {

class ProjectSessionStore {
public:
    static constexpr const char* CURRENT_SESSION_PATH = "session/current.mspj";
    static constexpr const char* CURRENT_SESSION_BACKUP_PATH = "session/current.bak";
    static constexpr const char* CURRENT_SESSION_TMP_PATH = "tmp/session.current.tmp";

    explicit ProjectSessionStore(ProductFileService& files);

    oc::type::Result<ProjectSaveResult> saveCurrent(
        const core::state::project::ProjectSnapshot& snapshot
    );

    oc::type::Result<ProjectLoadResult> loadCurrent(
        core::state::project::ProjectSnapshot& out,
        core::persistence::project_file::LoadReport* report = nullptr
    );

private:
    ProductFileService& files_;
};

}  // namespace core::persistence

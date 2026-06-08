#pragma once

#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductFileService.hpp"
#include "persistence/ProjectLoadReport.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::persistence {

struct ProjectSaveResult {
    uint32_t bytesWritten = 0;
    char projectPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
};

struct ProjectLoadResult {
    uint32_t bytesRead = 0;
    core::persistence::project_file::LoadStatus loadStatus =
        core::persistence::project_file::LoadStatus::OK;
    bool overwriteSafe = true;
    char projectPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
};

class ProjectFileStore {
public:
    static constexpr uint32_t MAX_PROJECT_FILE_SIZE = 98304;
    static constexpr uint32_t WRITE_CHUNK_SIZE = 4096;

    explicit ProjectFileStore(ProductFileService& files);

    oc::type::Result<ProjectSaveResult> save(
        const core::state::project::ProjectSnapshot& snapshot
    );

    oc::type::Result<ProjectLoadResult> load(
        const char* projectId,
        core::state::project::ProjectSnapshot& out,
        core::persistence::project_file::LoadReport* report = nullptr
    );

private:
    struct ProjectPaths {
        char directory[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
        char current[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
        char backup[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
        char tmp[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    };

    static bool buildPaths_(const char* projectId, ProjectPaths& out);
    static bool validProjectId_(const char* projectId);
    static oc::type::Result<void> invalid_(const char* context);
    static oc::type::Result<void> storageWriteFailed_(const char* context);
    static oc::type::Result<void> storageReadFailed_(const char* context);
    static oc::type::Result<void> resourceExhausted_(const char* context);

    oc::type::Result<void> removeIfExists_(const char* path);
    oc::type::Result<void> writeTmp_(const char* tmpPath, const uint8_t* data, uint32_t size);
    oc::type::Result<void> commitTmp_(const ProjectPaths& paths);

    ProductFileService& files_;
};

}  // namespace core::persistence

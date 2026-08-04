#pragma once

#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductFileService.hpp"
#include "persistence/ProductDirectoryCatalog.hpp"
#include "persistence/ProjectFileLimits.hpp"
#include "persistence/ProjectLoadReport.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::persistence {

struct ProjectSaveResult {
    uint32_t bytesWritten = 0;
    char projectPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
};

struct ProjectLoadResult {
    uint32_t bytesRead = 0;
    char projectPath[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
};

struct ProjectListEntry {
    char id[core::state::project::ProjectMetadata::ID_SIZE] = {};
    uint32_t sizeBytes = 0;
};

struct ProjectListResult {
    uint8_t count = 0;
    bool truncated = false;
};

class ProjectFileStore {
public:
    static constexpr uint32_t MAX_PROJECT_FILE_SIZE = PROJECT_FILE_MAX_SIZE;
    static constexpr uint32_t WRITE_CHUNK_SIZE = PROJECT_FILE_WRITE_CHUNK_SIZE;

    ProjectFileStore(
        ProductFileService& files,
        ProductDirectoryCatalog& catalog
    );

    oc::type::Result<ProjectSaveResult> save(
        const core::state::project::ProjectSnapshot& snapshot
    );

    oc::type::Result<ProjectLoadResult> load(
        const char* projectId,
        core::state::project::ProjectSnapshot& out,
        core::persistence::project_file::LoadReport* report = nullptr
    );

    oc::type::Result<ProjectListResult> listProjects(ProjectListEntry* entries,
                                                     uint8_t capacity);
    oc::type::Result<void> nextProjectId(char* out, size_t outSize);

private:
    struct ProjectPaths {
        char directory[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
        char current[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
        char backup[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
        char tmp[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    };

    static bool buildPaths_(const char* projectId, ProjectPaths& out);
    static bool validProjectId_(const char* projectId);

    static oc::type::Result<ProjectSaveResult> saveWithPaths_(
        ProductFileService& files,
        const core::state::project::ProjectSnapshot& snapshot,
        const ProjectPaths& paths
    );

    ProductFileService& files_;
    ProductDirectoryCatalog& catalog_;
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(ProjectFileStore) == 8U, "project file store ABI drift");
static_assert(alignof(ProjectFileStore) == 4U, "project file store alignment drift");
#endif

}  // namespace core::persistence

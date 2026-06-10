#pragma once

#include <cstdint>

namespace core::state {
struct CoreState;
}

namespace core::persistence {
class ProductFileService;
namespace project_file {
enum class LoadStatus : uint8_t;
}
}  // namespace core::persistence

namespace core::handler {

class ProjectLifecycleDomainServices {
public:
    enum class Status : uint8_t {
        OK = 0,
        UNAVAILABLE,
        INVALID_ARGUMENT,
        ALREADY_EXISTS,
        SAVE_FAILED,
        LOAD_FAILED,
        LIST_FAILED,
        PARTIAL_LOAD,
        UNSAFE_OVERWRITE,
    };

    struct Result {
        Status status = Status::UNAVAILABLE;
        uint32_t bytes = 0;
        core::persistence::project_file::LoadStatus loadStatus{};
        bool overwriteSafe = true;

        bool success() const {
            return status == Status::OK || status == Status::PARTIAL_LOAD;
        }
    };

    ProjectLifecycleDomainServices() = default;
    explicit ProjectLifecycleDomainServices(core::state::CoreState& state);
    ProjectLifecycleDomainServices(core::state::CoreState& state,
                                   core::persistence::ProductFileService& productFiles);
    static ProjectLifecycleDomainServices fromCoreState(core::state::CoreState& state);
    static ProjectLifecycleDomainServices fromCoreState(
        core::state::CoreState& state,
        core::persistence::ProductFileService& productFiles
    );

    Result resetMusicalProject() const;
    const char* currentProjectId() const;
    bool currentProjectDirty() const;
    bool currentProjectHasSavedIdentity() const;
    bool currentProjectOverwriteSafe() const;
    Result markProjectMutated() const;
    Result saveCurrentProject() const;
    Result saveAsNextProject() const;
    Result saveAsProject(const char* projectId) const;
    Result renameCurrentProject(const char* projectId) const;
    Result saveProject(const char* projectId) const;
    Result loadProject(const char* projectId) const;
    Result refreshLoadableProjects() const;

private:
    core::state::CoreState* state_ = nullptr;
    core::persistence::ProductFileService* product_files_ = nullptr;
};

}  // namespace core::handler

#pragma once

#include <cstdint>

namespace core::state {
struct CoreState;
}

namespace core::persistence {
class ProductDirectoryCatalog;
class ProductFileService;
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
        DRAFT_ACTIVE,
        QUEUED,
    };

    struct Result {
        Status status = Status::UNAVAILABLE;
        uint32_t bytes = 0;

        bool success() const { return status == Status::OK; }
    };

    ProjectLifecycleDomainServices() = default;
    explicit ProjectLifecycleDomainServices(core::state::CoreState& state);
    ProjectLifecycleDomainServices(
        core::state::CoreState& state,
        core::persistence::ProductFileService& productFiles,
        core::persistence::ProductDirectoryCatalog& productCatalog
    );
    static ProjectLifecycleDomainServices fromCoreState(core::state::CoreState& state);
    static ProjectLifecycleDomainServices fromCoreState(
        core::state::CoreState& state,
        core::persistence::ProductFileService& productFiles,
        core::persistence::ProductDirectoryCatalog& productCatalog
    );

    Result resetMusicalProject() const;
    const char* currentProjectId() const;
    bool currentProjectDirty() const;
    bool currentProjectHasSavedIdentity() const;
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
    core::persistence::ProductDirectoryCatalog* product_catalog_ = nullptr;
};

}  // namespace core::handler

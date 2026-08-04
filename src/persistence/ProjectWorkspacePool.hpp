#pragma once

#include <cstddef>

namespace core::persistence {

class ProductFileService;
class ProjectFileReadWorkspace;
class ProjectFileWriteWorkspace;

/**
 * Inline control owner for the one Project persistence workspace.
 *
 * The concrete writer remains hidden in the implementation so including
 * ProductFileService does not pull the Project codec/state graph into every
 * filesystem consumer. ProductFileService is the only access route and binds
 * each borrow to its existing exact mutation lease.
 */
class ProjectWorkspacePool final {
public:
    ProjectWorkspacePool();
    ~ProjectWorkspacePool();

    ProjectWorkspacePool(const ProjectWorkspacePool&) = delete;
    ProjectWorkspacePool& operator=(const ProjectWorkspacePool&) = delete;
    ProjectWorkspacePool(ProjectWorkspacePool&& other) noexcept;
    ProjectWorkspacePool& operator=(ProjectWorkspacePool&& other) noexcept;

private:
    friend class ProductFileService;

    bool prepare();
    ProjectFileReadWorkspace& read();
    ProjectFileWriteWorkspace& write();

    ProjectFileWriteWorkspace& writer_();

    alignas(void*) std::byte writer_storage_[2U * sizeof(void*)]{};
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(ProjectWorkspacePool) == 8U, "Project workspace pool exceeds LOCK-S");
static_assert(alignof(ProjectWorkspacePool) == 4U, "Project workspace pool alignment drift");
#endif

}  // namespace core::persistence

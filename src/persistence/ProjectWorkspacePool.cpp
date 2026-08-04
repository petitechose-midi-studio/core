#include "persistence/ProjectWorkspacePool.hpp"

#include <new>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "persistence/ProjectFileWorkspace.hpp"

namespace core::persistence {

FLASHMEM ProjectWorkspacePool::ProjectWorkspacePool() {
    static_assert(
        sizeof(ProjectFileWriteWorkspace) == sizeof(writer_storage_),
        "Project workspace pool control size drift"
    );
    static_assert(
        alignof(ProjectFileWriteWorkspace) <= alignof(ProjectWorkspacePool),
        "Project workspace pool control alignment drift"
    );
    new (writer_storage_) ProjectFileWriteWorkspace{};
}

FLASHMEM ProjectWorkspacePool::~ProjectWorkspacePool() {
    writer_().~ProjectFileWriteWorkspace();
}

FLASHMEM ProjectWorkspacePool::ProjectWorkspacePool(
    ProjectWorkspacePool&& other
) noexcept {
    new (writer_storage_) ProjectFileWriteWorkspace(std::move(other.writer_()));
}

FLASHMEM ProjectWorkspacePool& ProjectWorkspacePool::operator=(
    ProjectWorkspacePool&& other
) noexcept {
    if (this != &other) {
        writer_().~ProjectFileWriteWorkspace();
        new (writer_storage_) ProjectFileWriteWorkspace(std::move(other.writer_()));
    }
    return *this;
}

FLASHMEM bool ProjectWorkspacePool::prepare() {
    return writer_().prepare();
}

FLASHMEM ProjectFileReadWorkspace& ProjectWorkspacePool::read() {
    return writer_();
}

FLASHMEM ProjectFileWriteWorkspace& ProjectWorkspacePool::write() {
    return writer_();
}

FLASHMEM ProjectFileWriteWorkspace& ProjectWorkspacePool::writer_() {
    return *std::launder(
        reinterpret_cast<ProjectFileWriteWorkspace*>(writer_storage_)
    );
}

}  // namespace core::persistence

#include "handler/project/ProjectLifecycleDomainServices.hpp"

#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/ProjectFileStore.hpp"
#include "persistence/ProductFileService.hpp"
#include "state/CoreState.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::handler {

namespace {

using Result = ProjectLifecycleDomainServices::Result;
using Status = ProjectLifecycleDomainServices::Status;
using oc::type::ErrorCode;

FLASHMEM Result unavailable() {
    return Result{.status = Status::UNAVAILABLE};
}

FLASHMEM Result invalidArgument() {
    return Result{.status = Status::INVALID_ARGUMENT};
}

FLASHMEM bool copyProjectId(core::state::project::ProjectMetadata& metadata,
                            const char* projectId) {
    if (projectId == nullptr || projectId[0] == '\0') return false;
    const size_t length = std::strlen(projectId);
    if (length >= metadata.id.size()) return false;
    metadata.id.fill('\0');
    std::memcpy(metadata.id.data(), projectId, length);
    metadata.id[length] = '\0';
    return true;
}

FLASHMEM bool assignText(char* target, size_t size, const char* source) {
    if (target == nullptr || size == 0) return false;
    target[0] = '\0';
    if (source == nullptr) return true;
    const size_t length = std::strlen(source);
    if (length >= size) return false;
    std::memcpy(target, source, length);
    target[length] = '\0';
    return true;
}

FLASHMEM bool formatGeneratedProjectId(uint16_t index, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || index == 0 || index > 999) return false;
    const int written = std::snprintf(out, outSize, "P%03u", static_cast<unsigned>(index));
    return written > 0 && static_cast<size_t>(written) < outSize;
}

FLASHMEM bool formatGeneratedProjectName(uint16_t index, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || index == 0 || index > 999) return false;
    const int written = std::snprintf(
        out,
        outSize,
        "Project %03u",
        static_cast<unsigned>(index)
    );
    return written > 0 && static_cast<size_t>(written) < outSize;
}

FLASHMEM bool shouldUseGeneratedProjectName(
    const core::state::project::ProjectMetadata& metadata
) {
    return metadata.name[0] == '\0' || std::strcmp(metadata.name.data(), "Untitled") == 0;
}

FLASHMEM bool formatProjectFilePath(const char* projectId, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || projectId == nullptr || projectId[0] == '\0') {
        return false;
    }
    const int written = std::snprintf(out, outSize, "projects/%s/project.mspj", projectId);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

FLASHMEM Result findNextProjectId(core::persistence::ProductFileService& files,
                                  char* outId,
                                  size_t outIdSize,
                                  uint16_t& outIndex) {
    char candidate[core::state::project::ProjectMetadata::ID_SIZE] = {};
    char path[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    for (uint16_t i = 1; i <= 999; ++i) {
        if (!formatGeneratedProjectId(i, candidate, sizeof(candidate)) ||
            !formatProjectFilePath(candidate, path, sizeof(path))) {
            return invalidArgument();
        }

        auto info = files.stat(path);
        if (!info && info.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
            if (!assignText(outId, outIdSize, candidate)) return invalidArgument();
            outIndex = i;
            return Result{.status = Status::OK};
        }
        if (!info) {
            return Result{.status = Status::LIST_FAILED};
        }
    }
    return Result{.status = Status::SAVE_FAILED};
}

}  // namespace

FLASHMEM ProjectLifecycleDomainServices::ProjectLifecycleDomainServices(
    core::state::CoreState& state
) : state_(&state) {}

FLASHMEM ProjectLifecycleDomainServices::ProjectLifecycleDomainServices(
    core::state::CoreState& state,
    core::persistence::ProductFileService& productFiles
) : state_(&state), product_files_(&productFiles) {}

FLASHMEM ProjectLifecycleDomainServices ProjectLifecycleDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return ProjectLifecycleDomainServices{state};
}

FLASHMEM ProjectLifecycleDomainServices ProjectLifecycleDomainServices::fromCoreState(
    core::state::CoreState& state,
    core::persistence::ProductFileService& productFiles
) {
    return ProjectLifecycleDomainServices{state, productFiles};
}

FLASHMEM ProjectLifecycleDomainServices::Result
ProjectLifecycleDomainServices::resetMusicalProject() const {
    if (state_ == nullptr) {
        return unavailable();
    }
    state_->resetMusicalProject();
    state_->requestProjectSessionSave();
    return Result{.status = Status::OK};
}

FLASHMEM const char* ProjectLifecycleDomainServices::currentProjectId() const {
    return state_ ? state_->project.metadata.id.data() : "";
}

FLASHMEM bool ProjectLifecycleDomainServices::currentProjectDirty() const {
    return state_ != nullptr && state_->project.metadata.dirty;
}

FLASHMEM bool ProjectLifecycleDomainServices::currentProjectHasSavedIdentity() const {
    return state_ != nullptr && state_->project.metadata.hasSavedIdentity;
}

FLASHMEM ProjectLifecycleDomainServices::Result
ProjectLifecycleDomainServices::markProjectMutated() const {
    if (state_ == nullptr) {
        return unavailable();
    }

    state_->markProjectMutated();
    return Result{.status = Status::OK};
}

FLASHMEM ProjectLifecycleDomainServices::Result ProjectLifecycleDomainServices::saveProject(
    const char* projectId
) const {
    if (state_ == nullptr || product_files_ == nullptr) {
        return unavailable();
    }
    if (projectId == nullptr || projectId[0] == '\0') {
        return invalidArgument();
    }

    core::state::project::ProjectSnapshot snapshot;
    if (!core::state::project::captureProjectSnapshot(*state_, snapshot)) {
        return Result{.status = Status::SAVE_FAILED};
    }
    if (!copyProjectId(snapshot.project.metadata, projectId)) {
        return invalidArgument();
    }
    snapshot.project.metadata.hasSavedIdentity = true;
    snapshot.project.metadata.dirty = false;

    core::persistence::ProjectFileStore store(*product_files_);
    auto saved = store.save(snapshot);
    if (!saved) {
        return Result{.status = Status::SAVE_FAILED};
    }

    state_->project.metadata = snapshot.project.metadata;
    state_->requestProjectSessionSave();
    state_->projectNavigation.notifyContentChanged();
    return Result{.status = Status::OK, .bytes = saved.value().bytesWritten};
}

FLASHMEM ProjectLifecycleDomainServices::Result
ProjectLifecycleDomainServices::saveCurrentProject() const {
    if (state_ == nullptr) {
        return unavailable();
    }
    if (!state_->project.metadata.hasSavedIdentity) {
        return saveAsNextProject();
    }
    return saveProject(state_->project.metadata.id.data());
}

FLASHMEM ProjectLifecycleDomainServices::Result
ProjectLifecycleDomainServices::saveAsNextProject() const {
    if (state_ == nullptr || product_files_ == nullptr) {
        return unavailable();
    }

    char nextId[core::state::project::ProjectMetadata::ID_SIZE] = {};
    uint16_t nextIndex = 0;
    auto selected = findNextProjectId(*product_files_, nextId, sizeof(nextId), nextIndex);
    if (!selected.success()) {
        return selected;
    }

    core::state::project::ProjectSnapshot snapshot;
    if (!core::state::project::captureProjectSnapshot(*state_, snapshot)) {
        return Result{.status = Status::SAVE_FAILED};
    }
    if (!copyProjectId(snapshot.project.metadata, nextId)) {
        return invalidArgument();
    }
    if (shouldUseGeneratedProjectName(snapshot.project.metadata)) {
        if (!formatGeneratedProjectName(
                nextIndex,
                snapshot.project.metadata.name.data(),
                snapshot.project.metadata.name.size()
            )) {
            return invalidArgument();
        }
    }
    snapshot.project.metadata.hasSavedIdentity = true;
    snapshot.project.metadata.dirty = false;

    core::persistence::ProjectFileStore store(*product_files_);
    auto saved = store.save(snapshot);
    if (!saved) {
        return Result{.status = Status::SAVE_FAILED};
    }

    state_->project.metadata = snapshot.project.metadata;
    state_->requestProjectSessionSave();
    state_->projectNavigation.notifyContentChanged();
    return Result{.status = Status::OK, .bytes = saved.value().bytesWritten};
}

FLASHMEM ProjectLifecycleDomainServices::Result ProjectLifecycleDomainServices::loadProject(
    const char* projectId
) const {
    if (state_ == nullptr || product_files_ == nullptr) {
        return unavailable();
    }
    if (projectId == nullptr || projectId[0] == '\0') {
        return invalidArgument();
    }

    core::persistence::ProjectFileStore store(*product_files_);
    core::state::project::ProjectSnapshot snapshot;
    core::persistence::project_file::LoadReport report{};
    auto loaded = store.load(projectId, snapshot, &report);
    if (!loaded) {
        return Result{.status = Status::LOAD_FAILED};
    }
    if (!core::state::project::applyProjectSnapshot(*state_, snapshot)) {
        return Result{.status = Status::LOAD_FAILED};
    }

    const bool partial =
        loaded.value().loadStatus == core::persistence::project_file::LoadStatus::PARTIAL;
    state_->requestProjectSessionSave();
    state_->projectNavigation.notifyContentChanged();
    return Result{
        .status = partial ? Status::PARTIAL_LOAD : Status::OK,
        .bytes = loaded.value().bytesRead,
        .loadStatus = loaded.value().loadStatus,
        .overwriteSafe = loaded.value().overwriteSafe,
    };
}

FLASHMEM ProjectLifecycleDomainServices::Result
ProjectLifecycleDomainServices::refreshLoadableProjects() const {
    if (state_ == nullptr || product_files_ == nullptr) {
        return unavailable();
    }

    state_->projectNavigation.loadProjects.clear();

    core::persistence::ProjectFileStore store(*product_files_);
    core::persistence::ProjectListEntry entries[
        core::state::project::ProjectBrowserState::MAX_PROJECTS
    ]{};
    auto listed = store.listProjects(
        entries,
        core::state::project::ProjectBrowserState::MAX_PROJECTS
    );
    if (!listed) {
        state_->projectNavigation.notifyContentChanged();
        return Result{.status = Status::LIST_FAILED};
    }

    state_->projectNavigation.loadProjects.scanned = true;
    state_->projectNavigation.loadProjects.truncated = listed.value().truncated;
    for (uint8_t i = 0; i < listed.value().count; ++i) {
        state_->projectNavigation.loadProjects.add(entries[i].id, entries[i].sizeBytes);
    }
    state_->projectNavigation.loadProjects.truncated =
        state_->projectNavigation.loadProjects.truncated || listed.value().truncated;
    state_->projectNavigation.notifyContentChanged();
    return Result{.status = Status::OK, .bytes = listed.value().count};
}

}  // namespace core::handler

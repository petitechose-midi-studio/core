#include "handler/project/ProjectLifecycleDomainServices.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "persistence/ProjectFileStore.hpp"
#include "persistence/ProductFileService.hpp"
#include "state/CoreState.hpp"
#include "state/project/ProjectSlug.hpp"
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

FLASHMEM Result alreadyExists() {
    return Result{.status = Status::ALREADY_EXISTS};
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

FLASHMEM bool formatProjectFilePath(const char* projectId, char* out, size_t outSize) {
    if (out == nullptr || outSize == 0 || projectId == nullptr || projectId[0] == '\0') {
        return false;
    }
    const int written = std::snprintf(out, outSize, "projects/%s.mspj", projectId);
    return written > 0 && static_cast<size_t>(written) < outSize;
}

FLASHMEM Result ensureProjectDoesNotExist(core::persistence::ProductFileService& files,
                                          const char* projectId) {
    char path[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    if (!formatProjectFilePath(projectId, path, sizeof(path))) {
        return invalidArgument();
    }

    auto info = files.stat(path);
    if (info) {
        return alreadyExists();
    }
    if (info.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
        return Result{.status = Status::OK};
    }
    return Result{.status = Status::LIST_FAILED};
}

FLASHMEM Result deleteProjectFileIfExists(
    core::persistence::ProductFileService& files,
    const char* projectId
) {
    char path[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1] = {};
    if (!formatProjectFilePath(projectId, path, sizeof(path))) {
        return invalidArgument();
    }

    auto acquired = files.acquireMutation(
        core::persistence::ProductMutationOwner::PROJECT
    );
    if (!acquired) {
        return Result{.status = Status::SAVE_FAILED};
    }
    auto lease = std::move(acquired.value());
    auto removed = files.remove(lease, path);
    auto released = files.releaseMutation(lease);
    if (!released) {
        return Result{.status = Status::SAVE_FAILED};
    }
    if (removed || removed.error().code == ErrorCode::RESOURCE_NOT_FOUND) {
        return Result{.status = Status::OK};
    }
    return Result{.status = Status::SAVE_FAILED};
}

}  // namespace

FLASHMEM ProjectLifecycleDomainServices::ProjectLifecycleDomainServices(
    core::state::CoreState& state
) : state_(&state) {}

FLASHMEM ProjectLifecycleDomainServices::ProjectLifecycleDomainServices(
    core::state::CoreState& state,
    core::persistence::ProductFileService& productFiles,
    core::persistence::ProductDirectoryCatalog& productCatalog
) : state_(&state),
    product_files_(&productFiles),
    product_catalog_(&productCatalog) {}

FLASHMEM ProjectLifecycleDomainServices ProjectLifecycleDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return ProjectLifecycleDomainServices{state};
}

FLASHMEM ProjectLifecycleDomainServices ProjectLifecycleDomainServices::fromCoreState(
    core::state::CoreState& state,
    core::persistence::ProductFileService& productFiles,
    core::persistence::ProductDirectoryCatalog& productCatalog
) {
    return ProjectLifecycleDomainServices{state, productFiles, productCatalog};
}

FLASHMEM ProjectLifecycleDomainServices::Result
ProjectLifecycleDomainServices::resetMusicalProject() const {
    if (state_ == nullptr) {
        return unavailable();
    }
    if (state_->sequencer.stepContentDraft.active.get()) {
        state_->sequencer.stepContentDraft.noteBlockedTransition(
            core::state::sequencer::
                SequencerStepContentDraftBlockedTransition::RESET
        );
        return Result{.status = Status::DRAFT_ACTIVE};
    }
    state_->resetMusicalProject();
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
    if (state_ == nullptr || product_files_ == nullptr || product_catalog_ == nullptr) {
        return unavailable();
    }
    if (projectId == nullptr || projectId[0] == '\0') {
        return invalidArgument();
    }

    auto snapshot = core::state::project::captureProjectSnapshotOwned(*state_);
    if (!snapshot) {
        return Result{.status = Status::SAVE_FAILED};
    }
    if (!core::state::project::assignProjectSlug(snapshot->project.metadata, projectId)) {
        return invalidArgument();
    }
    snapshot->project.metadata.hasSavedIdentity = true;
    snapshot->project.metadata.dirty = false;

    core::persistence::ProjectFileStore store(*product_files_, *product_catalog_);
    auto saved = store.save(*snapshot);
    if (!saved) {
        return Result{.status = Status::SAVE_FAILED};
    }

    const bool identityChanged =
        state_->project.metadata.hasSavedIdentity != snapshot->project.metadata.hasSavedIdentity ||
        std::strcmp(state_->project.metadata.id.data(),
                    snapshot->project.metadata.id.data()) != 0;
    state_->project.metadata = snapshot->project.metadata;
    if (identityChanged) {
        state_->publishProjectSessionReplacement_();
    } else {
        state_->requestProjectSessionSave();
    }
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
    if (state_ == nullptr || product_files_ == nullptr || product_catalog_ == nullptr) {
        return unavailable();
    }

    char nextId[core::state::project::ProjectMetadata::ID_SIZE] = {};
    core::persistence::ProjectFileStore store(*product_files_, *product_catalog_);
    const auto selected = store.nextProjectId(nextId, sizeof(nextId));
    if (!selected) {
        return Result{
            .status = selected.error().code == ErrorCode::HARDWARE_BUSY
                ? Status::QUEUED
                : (selected.error().code == ErrorCode::RESOURCE_EXHAUSTED
                    ? Status::SAVE_FAILED
                    : Status::LIST_FAILED),
        };
    }

    auto snapshot = core::state::project::captureProjectSnapshotOwned(*state_);
    if (!snapshot) {
        return Result{.status = Status::SAVE_FAILED};
    }
    if (!core::state::project::assignProjectSlug(snapshot->project.metadata, nextId)) {
        return invalidArgument();
    }
    snapshot->project.metadata.hasSavedIdentity = true;
    snapshot->project.metadata.dirty = false;

    auto saved = store.save(*snapshot);
    if (!saved) {
        return Result{.status = Status::SAVE_FAILED};
    }

    state_->project.metadata = snapshot->project.metadata;
    state_->publishProjectSessionReplacement_();
    state_->projectNavigation.notifyContentChanged();
    return Result{.status = Status::OK, .bytes = saved.value().bytesWritten};
}

FLASHMEM ProjectLifecycleDomainServices::Result ProjectLifecycleDomainServices::saveAsProject(
    const char* projectId
) const {
    if (state_ == nullptr || product_files_ == nullptr || product_catalog_ == nullptr) {
        return unavailable();
    }
    if (!core::state::project::validProjectSlug(projectId)) {
        return invalidArgument();
    }

    const auto available = ensureProjectDoesNotExist(*product_files_, projectId);
    if (!available.success()) {
        return available;
    }
    return saveProject(projectId);
}

FLASHMEM ProjectLifecycleDomainServices::Result
ProjectLifecycleDomainServices::renameCurrentProject(const char* projectId) const {
    if (state_ == nullptr || product_files_ == nullptr || product_catalog_ == nullptr) {
        return unavailable();
    }
    if (!core::state::project::validProjectSlug(projectId)) {
        return invalidArgument();
    }

    const char* currentId = state_->project.metadata.id.data();
    const bool hasCurrentIdentity =
        state_->project.metadata.hasSavedIdentity && currentId[0] != '\0';
    if (hasCurrentIdentity && std::strcmp(currentId, projectId) == 0) {
        return Result{.status = Status::OK};
    }

    const auto available = ensureProjectDoesNotExist(*product_files_, projectId);
    if (!available.success()) {
        return available;
    }

    char previousId[core::state::project::ProjectMetadata::ID_SIZE] = {};
    if (hasCurrentIdentity && !assignText(previousId, sizeof(previousId), currentId)) {
        return invalidArgument();
    }

    auto snapshot = core::state::project::captureProjectSnapshotOwned(*state_);
    if (!snapshot) {
        return Result{.status = Status::SAVE_FAILED};
    }
    if (!core::state::project::assignProjectSlug(snapshot->project.metadata, projectId)) {
        return invalidArgument();
    }
    snapshot->project.metadata.hasSavedIdentity = true;
    snapshot->project.metadata.dirty = false;

    core::persistence::ProjectFileStore store(*product_files_, *product_catalog_);
    auto saved = store.save(*snapshot);
    if (!saved) {
        return Result{.status = Status::SAVE_FAILED};
    }

    if (hasCurrentIdentity) {
        const auto deleted =
            deleteProjectFileIfExists(*product_files_, previousId);
        if (!deleted.success()) {
            deleteProjectFileIfExists(*product_files_, projectId);
            return deleted;
        }
    }

    state_->project.metadata = snapshot->project.metadata;
    state_->publishProjectSessionReplacement_();
    state_->projectNavigation.notifyContentChanged();
    return Result{.status = Status::OK, .bytes = saved.value().bytesWritten};
}

FLASHMEM ProjectLifecycleDomainServices::Result ProjectLifecycleDomainServices::loadProject(
    const char* projectId
) const {
    if (state_ == nullptr || product_files_ == nullptr || product_catalog_ == nullptr) {
        return unavailable();
    }
    if (state_->sequencer.stepContentDraft.active.get()) {
        state_->sequencer.stepContentDraft.noteBlockedTransition(
            core::state::sequencer::
                SequencerStepContentDraftBlockedTransition::PROJECT_LOAD
        );
        return Result{.status = Status::DRAFT_ACTIVE};
    }
    if (projectId == nullptr || projectId[0] == '\0') {
        return invalidArgument();
    }

    core::persistence::ProjectFileStore store(*product_files_, *product_catalog_);
    auto snapshot = core::state::project::makeProjectSnapshot();
    if (!snapshot) {
        return Result{.status = Status::LOAD_FAILED};
    }
    core::persistence::project_file::LoadReport report{};
    auto loaded = store.load(projectId, *snapshot, &report);
    if (!loaded) {
        return Result{.status = Status::LOAD_FAILED};
    }
    if (!core::state::project::applyProjectSnapshot(*state_, *snapshot)) {
        return Result{.status = Status::LOAD_FAILED};
    }

    state_->requestProjectSessionSave();
    state_->projectNavigation.notifyContentChanged();
    return Result{
        .status = Status::OK,
        .bytes = loaded.value().bytesRead,
    };
}

FLASHMEM ProjectLifecycleDomainServices::Result
ProjectLifecycleDomainServices::refreshLoadableProjects() const {
    if (state_ == nullptr || product_files_ == nullptr || product_catalog_ == nullptr) {
        return unavailable();
    }

    core::persistence::ProjectFileStore store(*product_files_, *product_catalog_);
    core::persistence::ProjectListEntry entries[
        core::state::project::ProjectBrowserState::MAX_PROJECTS
    ]{};
    auto listed = store.listProjects(
        entries,
        core::state::project::ProjectBrowserState::MAX_PROJECTS
    );
    if (!listed) {
        if (listed.error().code == ErrorCode::HARDWARE_BUSY) {
            return Result{.status = Status::QUEUED};
        }
        state_->projectNavigation.loadProjects.clear();
        state_->projectNavigation.notifyContentChanged();
        return Result{.status = Status::LIST_FAILED};
    }

    state_->projectNavigation.loadProjects.clear();
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

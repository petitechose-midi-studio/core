#include "persistence/ProjectSessionAutosaveService.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

#include "persistence/ProjectSessionStore.hpp"
#include "state/CoreState.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::persistence {

FLASHMEM ProjectSessionAutosaveService::ProjectSessionAutosaveService(
    ProductFileService& files,
    uint32_t delayMs
) : files_(files)
  , delay_ms_(delayMs == 0 ? core::state::CoreState::PROJECT_SESSION_AUTOSAVE_DELAY_MS
                           : delayMs) {}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::update(
    core::state::CoreState& state,
    uint32_t nowMs,
    bool writeBlocked
) {
    if (!state.hasPendingProjectSessionSave()) {
        return Result{.status = Status::IDLE};
    }

    if (writeBlocked) {
        return Result{.status = Status::BLOCKED};
    }

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    if (requestedAt == 0 ||
        static_cast<uint32_t>(nowMs - requestedAt) < delay_ms_) {
        return Result{.status = Status::WAITING};
    }

    return saveNow_(state);
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::flush(
    core::state::CoreState& state
) {
    if (!state.hasPendingProjectSessionSave()) {
        return Result{.status = Status::IDLE};
    }

    return saveNow_(state);
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::saveNow_(
    core::state::CoreState& state
) {
    core::state::project::ProjectSnapshot snapshot;
    if (!core::state::project::captureProjectSnapshot(state, snapshot)) {
        state.requestProjectSessionSave();
        OC_LOG_WARN("[ProjectSessionAutosave] capture failed");
        return Result{.status = Status::CAPTURE_FAILED};
    }

    const uint32_t savedCounter = snapshot.project.metadata.modifiedCounter;
    ProjectSessionStore store(files_);
    auto saved = store.saveCurrent(snapshot);
    if (!saved) {
        state.requestProjectSessionSave();
        OC_LOG_WARN("[ProjectSessionAutosave] save failed");
        return Result{.status = Status::SAVE_FAILED, .modifiedCounter = savedCounter};
    }

    state.acknowledgeProjectSessionSave(savedCounter);
    return Result{
        .status = Status::SAVED,
        .bytes = saved.value().bytesWritten,
        .modifiedCounter = savedCounter,
    };
}

}  // namespace core::persistence

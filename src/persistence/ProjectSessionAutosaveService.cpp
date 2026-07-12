#include "persistence/ProjectSessionAutosaveService.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/log/Log.hpp>

#include "persistence/ProjectSessionStore.hpp"
#include "state/CoreState.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::persistence {

FLASHMEM ProjectSessionAutosaveService::ProjectSessionAutosaveService(
    ProjectSessionStore& store,
    uint32_t delayMs
) : store_(store)
  , delay_ms_(delayMs == 0 ? core::state::CoreState::PROJECT_SESSION_AUTOSAVE_DELAY_MS
                           : delayMs) {
    OC_PERF_SCOPE(perfInitialize, "persistence.autosave.initialize");
    if (!store_.prepareWorkspace()) {
        OC_LOG_WARN("[ProjectSessionAutosave] file workspace allocation failed");
    }
    snapshot_ = core::state::project::makeProjectSnapshot();
    if (!snapshot_) {
        OC_LOG_WARN("[ProjectSessionAutosave] snapshot workspace allocation failed");
    }
}

FLASHMEM ProjectSessionAutosaveService::~ProjectSessionAutosaveService() {
    cancelInFlight_();
}

ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::update(
    core::state::CoreState& state,
    uint32_t nowMs,
    bool mutationPending
) {
    if (inProgress_() && mutationPending) {
        cancelInFlight_();
        return Result{.status = Status::BLOCKED};
    }

    if (store_.saveCurrentInProgress() &&
        state.project.metadata.modifiedCounter != captured_modified_counter_) {
        cancelInFlight_();
    }

    if (inProgress_()) {
        return capture_.active() ? advanceCapture_(state) : advanceSave_(state);
    }

    if (!state.hasPendingProjectSessionSave()) {
        return Result{.status = Status::IDLE};
    }

    if (mutationPending) {
        return Result{.status = Status::BLOCKED};
    }

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    if (static_cast<uint32_t>(nowMs - requestedAt) < delay_ms_) {
        return Result{.status = Status::WAITING};
    }

    return startCapture_(state);
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::flush(
    core::state::CoreState& state
) {
    if (capture_.active()) {
        cancelInFlight_();
    } else if (store_.saveCurrentInProgress() &&
               state.project.metadata.modifiedCounter != captured_modified_counter_) {
        cancelInFlight_();
    }

    if (!state.hasPendingProjectSessionSave() && !inProgress_()) {
        return Result{.status = Status::IDLE};
    }

    Result result{.status = Status::SAVING};
    if (!inProgress_()) {
        result = startCapture_(state);
    }

    while (result.status == Status::SAVING) {
        if (capture_.active()) {
            result = advanceCapture_(state);
        } else if (store_.saveCurrentInProgress()) {
            result = advanceSave_(state);
        } else {
            return Result{.status = Status::SAVE_FAILED};
        }
    }
    return result;
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::startCapture_(
    core::state::CoreState& state
) {
    OC_PERF_SCOPE(perfStart, "persistence.autosave.start-capture");
    if (!snapshot_) {
        snapshot_ = core::state::project::makeProjectSnapshot();
    }
    if (!snapshot_ || !capture_.begin(state, *snapshot_)) {
        state.requestProjectSessionSave();
        OC_LOG_WARN("[ProjectSessionAutosave] capture failed");
        return Result{.status = Status::CAPTURE_FAILED};
    }

    return advanceCapture_(state);
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::advanceCapture_(
    core::state::CoreState& state
) {
    OC_PERF_SCOPE(perfCapture, "persistence.autosave.capture-slice");
    const auto progress = capture_.advance();

    if (progress.status == core::state::project::ProjectSnapshotCapture::Status::STALE) {
        captured_modified_counter_ = 0;
        return Result{
            .status = Status::WAITING,
            .modifiedCounter = progress.modifiedCounter,
        };
    }
    if (progress.status == core::state::project::ProjectSnapshotCapture::Status::FAILED ||
        progress.status == core::state::project::ProjectSnapshotCapture::Status::IDLE) {
        state.requestProjectSessionSave();
        OC_LOG_WARN("[ProjectSessionAutosave] capture failed");
        return Result{.status = Status::CAPTURE_FAILED};
    }
    if (progress.status == core::state::project::ProjectSnapshotCapture::Status::IN_PROGRESS) {
        return Result{
            .status = Status::SAVING,
            .modifiedCounter = progress.modifiedCounter,
        };
    }

    captured_modified_counter_ = progress.modifiedCounter;
    auto started = store_.beginSaveCurrent(*snapshot_);
    if (!started) {
        state.requestProjectSessionSave();
        captured_modified_counter_ = 0;
        OC_LOG_WARN("[ProjectSessionAutosave] save start failed");
        return Result{.status = Status::SAVE_FAILED};
    }
    return Result{
        .status = Status::SAVING,
        .modifiedCounter = captured_modified_counter_,
    };
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::advanceSave_(
    core::state::CoreState& state
) {
    OC_PERF_SCOPE(perfSave, "persistence.autosave.save-slice");
    auto saved = store_.advanceSaveCurrent();

    if (!saved) {
        state.requestProjectSessionSave();
        const uint32_t savedCounter = captured_modified_counter_;
        captured_modified_counter_ = 0;
        OC_LOG_WARN("[ProjectSessionAutosave] save failed");
        return Result{
            .status = Status::SAVE_FAILED,
            .modifiedCounter = savedCounter,
        };
    }
    if (!saved.value().complete) {
        return Result{
            .status = Status::SAVING,
            .modifiedCounter = captured_modified_counter_,
        };
    }

    const uint32_t savedCounter = captured_modified_counter_;
    const uint32_t bytesWritten = saved.value().bytesWritten;
    OC_PERF_UNITS(perfSave, bytesWritten, 1U);

    state.acknowledgeProjectSessionSave(savedCounter);
    captured_modified_counter_ = 0;
    return Result{
        .status = Status::SAVED,
        .bytes = bytesWritten,
        .modifiedCounter = savedCounter,
    };
}

FLASHMEM void ProjectSessionAutosaveService::cancelInFlight_() {
    capture_.cancel();
    if (store_.saveCurrentInProgress()) {
        store_.cancelSaveCurrent();
    }
    captured_modified_counter_ = 0;
}

bool ProjectSessionAutosaveService::inProgress_() const {
    return capture_.active() || store_.saveCurrentInProgress();
}

bool ProjectSessionAutosaveService::writeSessionActive() const {
    return store_.saveCurrentWriteSessionActive();
}

}  // namespace core::persistence

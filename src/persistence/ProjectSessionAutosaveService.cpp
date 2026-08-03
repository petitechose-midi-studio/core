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
    const bool inProgress = inProgress_();
    if (!inProgress && !state.hasPendingProjectSessionSave()) {
        return Result{.status = Status::IDLE};
    }
    return updatePending_(state, nowMs, mutationPending, inProgress);
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::updatePending_(
    core::state::CoreState& state,
    uint32_t nowMs,
    bool mutationPending,
    bool inProgress
) {
    if (inProgress) {
        const bool transactionPending =
            mutationPending || state.hasPendingProjectTransaction();
        if (transactionPending) {
            cancelInFlight_();
            return Result{.status = Status::BLOCKED};
        }

        const auto* guard = capture_.guard();
        if (guard == nullptr ||
            !state.projectSessionSaveTokenMatches(guard->token)) {
            cancelInFlight_();
        }

        if (inProgress_()) {
            return capture_.active() ? advanceCapture_(state) : advanceSave_(state);
        }
    }

    if (!state.hasPendingProjectSessionSave()) {
        return Result{.status = Status::IDLE};
    }

    if (mutationPending || state.hasPendingProjectTransaction()) {
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
    if (state.hasPendingProjectTransaction()) {
        if (inProgress_()) cancelInFlight_();
        return Result{.status = Status::BLOCKED};
    }
    const auto* guard = capture_.guard();
    if (inProgress_() &&
        (guard == nullptr ||
         !state.projectSessionSaveTokenMatches(guard->token))) {
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

FLASHMEM ProjectSessionAutosaveService::Result
ProjectSessionAutosaveService::flushRecovery(
    core::state::CoreState& state,
    const ProductMutationLease& recoveryLease
) {
    // Recovery is synchronous and highest-integrity. Discard any capture/save
    // tied to the removed medium, then bind a fresh snapshot to the exact live
    // RAM identity before using the caller's sole RECOVERY lease.
    cancelInFlight_();
    if (state.hasPendingProjectTransaction()) {
        return Result{.status = Status::BLOCKED};
    }
    if (!snapshot_) {
        return Result{.status = Status::CAPTURE_FAILED};
    }

    const auto requestedToken = state.requestProjectSessionSave();
    if (!state.hasPendingProjectSessionSave() ||
        !state.projectSessionSaveTokenMatches(requestedToken) ||
        !capture_.begin(state, *snapshot_)) {
        capture_.cancel();
        return Result{
            .status = Status::CAPTURE_FAILED,
            .modifiedCounter = requestedToken.modifiedCounter,
        };
    }

    core::state::project::ProjectSnapshotCapture::Progress progress{};
    do {
        progress = capture_.advance();
        if (progress.status ==
            core::state::project::ProjectSnapshotCapture::Status::STALE) {
            return Result{
                .status = Status::WAITING,
                .modifiedCounter = progress.modifiedCounter,
            };
        }
        if (progress.status ==
                core::state::project::ProjectSnapshotCapture::Status::FAILED ||
            progress.status ==
                core::state::project::ProjectSnapshotCapture::Status::IDLE) {
            capture_.cancel();
            return Result{
                .status = Status::CAPTURE_FAILED,
                .modifiedCounter = progress.modifiedCounter,
            };
        }
    } while (progress.status ==
             core::state::project::ProjectSnapshotCapture::Status::IN_PROGRESS);

    const auto* guard = capture_.guard();
    if (progress.status !=
            core::state::project::ProjectSnapshotCapture::Status::COMPLETE ||
        guard == nullptr || !capture_.complete() ||
        !state.projectSessionSaveTokenMatches(guard->token)) {
        capture_.cancel();
        return Result{
            .status = Status::WAITING,
            .modifiedCounter = requestedToken.modifiedCounter,
        };
    }

    const auto capturedToken = guard->token;
    auto saved = store_.saveCurrent(*snapshot_, recoveryLease);
    if (!saved) {
        capture_.cancel();
        return Result{
            .status = Status::SAVE_FAILED,
            .modifiedCounter = capturedToken.modifiedCounter,
        };
    }

    const uint32_t bytesWritten = saved.value().bytesWritten;
    if (!state.acknowledgeProjectSessionSave(capturedToken)) {
        capture_.cancel();
        return Result{
            .status = Status::WAITING,
            .bytes = bytesWritten,
            .modifiedCounter = capturedToken.modifiedCounter,
        };
    }
    capture_.cancel();
    return Result{
        .status = Status::SAVED,
        .bytes = bytesWritten,
        .modifiedCounter = capturedToken.modifiedCounter,
    };
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

    const auto* guard = capture_.guard();
    if (guard == nullptr || !capture_.complete() ||
        !state.projectSessionSaveTokenMatches(guard->token)) {
        cancelInFlight_();
        return Result{
            .status = Status::WAITING,
            .modifiedCounter = progress.modifiedCounter,
        };
    }
    auto started = store_.beginSaveCurrent(*snapshot_);
    if (!started) {
        capture_.cancel();
        state.requestProjectSessionSave();
        OC_LOG_WARN("[ProjectSessionAutosave] save start failed");
        return Result{.status = Status::SAVE_FAILED};
    }
    return Result{
        .status = Status::SAVING,
        .modifiedCounter = guard->token.modifiedCounter,
    };
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::advanceSave_(
    core::state::CoreState& state
) {
    OC_PERF_SCOPE(perfSave, "persistence.autosave.save-slice");
    const auto* guard = capture_.guard();
    if (guard == nullptr || !capture_.complete() ||
        !state.projectSessionSaveTokenMatches(guard->token)) {
        const uint32_t staleCounter =
            guard != nullptr ? guard->token.modifiedCounter : 0U;
        cancelInFlight_();
        return Result{
            .status = Status::WAITING,
            .modifiedCounter = staleCounter,
        };
    }
    const auto capturedToken = guard->token;
    auto saved = store_.advanceSaveCurrent();

    if (!saved) {
        state.requestProjectSessionSave();
        capture_.cancel();
        OC_LOG_WARN("[ProjectSessionAutosave] save failed");
        return Result{
            .status = Status::SAVE_FAILED,
            .modifiedCounter = capturedToken.modifiedCounter,
        };
    }
    if (!saved.value().complete) {
        return Result{
            .status = Status::SAVING,
            .modifiedCounter = capturedToken.modifiedCounter,
        };
    }

    const uint32_t bytesWritten = saved.value().bytesWritten;
    OC_PERF_UNITS(perfSave, bytesWritten, 1U);

    state.acknowledgeProjectSessionSave(capturedToken);
    capture_.cancel();
    return Result{
        .status = Status::SAVED,
        .bytes = bytesWritten,
        .modifiedCounter = capturedToken.modifiedCounter,
    };
}

FLASHMEM void ProjectSessionAutosaveService::cancelInFlight_() {
    capture_.cancel();
    if (store_.saveCurrentInProgress()) {
        store_.cancelSaveCurrent();
    }
}

bool ProjectSessionAutosaveService::inProgress_() const {
    return capture_.active() || store_.saveCurrentInProgress();
}

bool ProjectSessionAutosaveService::writeSessionActive() const {
    return store_.saveCurrentWriteSessionActive();
}

}  // namespace core::persistence

#include "persistence/ProjectSessionAutosaveService.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/log/Log.hpp>

#include "persistence/ProjectSessionStore.hpp"
#include "state/CoreState.hpp"
#include "state/project/ProjectSnapshot.hpp"

namespace core::persistence {

namespace {

using oc::type::ErrorCode;

const char kRecoveryBlocked[] PROGMEM =
    "project session recovery blocked by live transaction";
const char kSnapshotUnavailable[] PROGMEM =
    "project session snapshot unavailable";
const char kCaptureStartFailed[] PROGMEM =
    "project session capture start failed";
const char kCaptureAdvanceFailed[] PROGMEM =
    "project session capture advance failed";
const char kSaveProgressLost[] PROGMEM =
    "project session save progress lost";
const char kRecoveryProgressLost[] PROGMEM =
    "project session recovery progress lost";
const char kSaveAcknowledgementFailed[] PROGMEM =
    "project session save acknowledgement failed";
const char kFailureStageNone[] PROGMEM = "NONE";
const char kFailureStageCapture[] PROGMEM = "CAPTURE";
const char kFailureStagePrepare[] PROGMEM = "PREPARE";
const char kFailureStageEncode[] PROGMEM = "ENCODE";
const char kFailureStageWrite[] PROGMEM = "WRITE";
const char kFailureStageCommit[] PROGMEM = "COMMIT";
const char kFailureStageAcknowledge[] PROGMEM = "ACKNOWLEDGE";

ProjectSessionAutosaveService::FailureStage failureStage(
    ProjectSaveStage stage
) {
    using FailureStage = ProjectSessionAutosaveService::FailureStage;
    switch (stage) {
        case ProjectSaveStage::ENCODE:
            return FailureStage::ENCODE;
        case ProjectSaveStage::WRITE:
            return FailureStage::WRITE;
        case ProjectSaveStage::COMMIT:
            return FailureStage::COMMIT;
        case ProjectSaveStage::PREPARE:
        default:
            return FailureStage::PREPARE;
    }
}

}  // namespace

FLASHMEM const char* ProjectSessionAutosaveService::failureStageLabel(
    FailureStage stage
) {
    switch (stage) {
        case FailureStage::CAPTURE: return kFailureStageCapture;
        case FailureStage::PREPARE: return kFailureStagePrepare;
        case FailureStage::ENCODE: return kFailureStageEncode;
        case FailureStage::WRITE: return kFailureStageWrite;
        case FailureStage::COMMIT: return kFailureStageCommit;
        case FailureStage::ACKNOWLEDGE: return kFailureStageAcknowledge;
        case FailureStage::NONE:
        default: return kFailureStageNone;
    }
}

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
    if (recovery_in_progress_) {
        return Result{
            .status = Status::BLOCKED,
            .error = ErrorCode::HARDWARE_BUSY,
            .errorContext = kRecoveryBlocked,
        };
    }
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
    if (recovery_in_progress_) {
        return Result{
            .status = Status::BLOCKED,
            .error = ErrorCode::HARDWARE_BUSY,
            .errorContext = kRecoveryBlocked,
        };
    }
    if (state.hasPendingProjectTransaction()) {
        if (inProgress_()) cancelInFlight_();
        return Result{
            .status = Status::BLOCKED,
            .error = ErrorCode::HARDWARE_BUSY,
            .errorContext = kRecoveryBlocked,
        };
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
            return Result{
                .status = Status::SAVE_FAILED,
                .error = ErrorCode::INVALID_STATE,
                .errorContext = kSaveProgressLost,
            };
        }
    }
    return result;
}

FLASHMEM ProjectSessionAutosaveService::Result
ProjectSessionAutosaveService::flushRecovery(
    core::state::CoreState& state,
    const ProductMutationLease& recoveryLease
) {
    Result result = beginRecovery(state, recoveryLease);
    while (result.status == Status::SAVING) {
        result = advanceRecovery(state, recoveryLease);
    }
    return result;
}

FLASHMEM ProjectSessionAutosaveService::Result
ProjectSessionAutosaveService::beginRecovery(
    core::state::CoreState& state,
    const ProductMutationLease& recoveryLease
) {
    // Discard work tied to the removed medium, then bind a fresh snapshot to
    // the exact live RAM identity and caller-owned RECOVERY lease.
    cancelInFlight_();
    if (!recoveryLease.valid()) {
        return Result{
            .status = Status::BLOCKED,
            .error = ErrorCode::INVALID_STATE,
            .errorContext = kRecoveryBlocked,
        };
    }
    if (state.hasPendingProjectTransaction()) {
        return Result{
            .status = Status::BLOCKED,
            .error = ErrorCode::HARDWARE_BUSY,
            .errorContext = kRecoveryBlocked,
        };
    }
    if (!snapshot_) {
        return Result{
            .status = Status::CAPTURE_FAILED,
            .failureStage = FailureStage::CAPTURE,
            .error = ErrorCode::RESOURCE_EXHAUSTED,
            .errorContext = kSnapshotUnavailable,
        };
    }

    const auto requestedToken = state.requestProjectSessionSave();
    if (!state.hasPendingProjectSessionSave() ||
        !state.projectSessionSaveTokenMatches(requestedToken) ||
        !capture_.begin(state, *snapshot_)) {
        capture_.cancel();
        return Result{
            .status = Status::CAPTURE_FAILED,
            .modifiedCounter = requestedToken.modifiedCounter,
            .failureStage = FailureStage::CAPTURE,
            .error = ErrorCode::RESOURCE_EXHAUSTED,
            .errorContext = kCaptureStartFailed,
        };
    }

    recovery_in_progress_ = true;
    return Result{
        .status = Status::SAVING,
        .modifiedCounter = requestedToken.modifiedCounter,
    };
}

FLASHMEM ProjectSessionAutosaveService::Result
ProjectSessionAutosaveService::advanceRecovery(
    core::state::CoreState& state,
    const ProductMutationLease& recoveryLease
) {
    if (!recovery_in_progress_) {
        return Result{
            .status = Status::SAVE_FAILED,
            .error = ErrorCode::INVALID_STATE,
            .errorContext = kRecoveryProgressLost,
        };
    }
    if (state.hasPendingProjectTransaction()) {
        cancelInFlight_();
        return Result{
            .status = Status::BLOCKED,
            .error = ErrorCode::HARDWARE_BUSY,
            .errorContext = kRecoveryBlocked,
        };
    }

    const auto* guard = capture_.guard();
    if (guard == nullptr ||
        !state.projectSessionSaveTokenMatches(guard->token)) {
        const uint32_t staleCounter = guard != nullptr
            ? guard->token.modifiedCounter
            : 0U;
        cancelInFlight_();
        return Result{
            .status = Status::WAITING,
            .modifiedCounter = staleCounter,
        };
    }
    if (capture_.active()) {
        return advanceRecoveryCapture_(state, recoveryLease);
    }
    if (store_.saveCurrentInProgress()) {
        return advanceSave_(state);
    }

    cancelInFlight_();
    return Result{
        .status = Status::SAVE_FAILED,
        .error = ErrorCode::INVALID_STATE,
        .errorContext = kRecoveryProgressLost,
    };
}

FLASHMEM void ProjectSessionAutosaveService::cancelRecovery() {
    cancelInFlight_();
}

ProductPersistenceWorkQuota
ProjectSessionAutosaveService::recoveryWorkQuota() const {
    if (capture_.active()) {
        return PRODUCT_PERSISTENCE_QUOTA_AUTOSAVE_SEQUENCER;
    }
    if (!store_.saveCurrentInProgress()) {
        return PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE;
    }
    switch (store_.saveCurrentStage()) {
        case ProjectSaveStage::ENCODE:
            return PRODUCT_PERSISTENCE_QUOTA_PROJECT_ENCODE;
        case ProjectSaveStage::WRITE:
            return PRODUCT_PERSISTENCE_QUOTA_ORDINARY_IO;
        case ProjectSaveStage::PREPARE:
        case ProjectSaveStage::COMMIT:
        default:
            return PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE;
    }
}

FLASHMEM ProjectSessionAutosaveService::Result
ProjectSessionAutosaveService::advanceRecoveryCapture_(
    core::state::CoreState& state,
    const ProductMutationLease& recoveryLease
) {
    OC_PERF_SCOPE(perfCapture, "persistence.autosave.recovery-capture-slice");
    const auto progress = capture_.advance();
    if (progress.status ==
        core::state::project::ProjectSnapshotCapture::Status::STALE) {
        cancelInFlight_();
        return Result{
            .status = Status::WAITING,
            .modifiedCounter = progress.modifiedCounter,
        };
    }
    if (progress.status ==
            core::state::project::ProjectSnapshotCapture::Status::FAILED ||
        progress.status ==
            core::state::project::ProjectSnapshotCapture::Status::IDLE) {
        cancelInFlight_();
        return Result{
            .status = Status::CAPTURE_FAILED,
            .modifiedCounter = progress.modifiedCounter,
            .failureStage = FailureStage::CAPTURE,
            .error = ErrorCode::RESOURCE_EXHAUSTED,
            .errorContext = kCaptureAdvanceFailed,
        };
    }
    if (progress.status ==
        core::state::project::ProjectSnapshotCapture::Status::IN_PROGRESS) {
        return Result{
            .status = Status::SAVING,
            .modifiedCounter = progress.modifiedCounter,
        };
    }

    const auto* guard = capture_.guard();
    if (progress.status !=
            core::state::project::ProjectSnapshotCapture::Status::COMPLETE ||
        guard == nullptr || !capture_.complete() ||
        !state.projectSessionSaveTokenMatches(guard->token)) {
        cancelInFlight_();
        return Result{
            .status = Status::WAITING,
            .modifiedCounter = progress.modifiedCounter,
        };
    }

    const uint32_t capturedCounter = guard->token.modifiedCounter;
    auto begun = store_.beginSaveCurrent(*snapshot_, recoveryLease);
    if (!begun) {
        const auto error = begun.error();
        cancelInFlight_();
        state.requestProjectSessionSave();
        return Result{
            .status = Status::SAVE_FAILED,
            .modifiedCounter = capturedCounter,
            .failureStage = FailureStage::PREPARE,
            .error = error.code,
            .errorContext = error.context,
        };
    }
    return Result{
        .status = Status::SAVING,
        .modifiedCounter = capturedCounter,
    };
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::startCapture_(
    core::state::CoreState& state
) {
    recovery_in_progress_ = false;
    OC_PERF_SCOPE(perfStart, "persistence.autosave.start-capture");
    if (!snapshot_) {
        snapshot_ = core::state::project::makeProjectSnapshot();
    }
    if (!snapshot_ || !capture_.begin(state, *snapshot_)) {
        state.requestProjectSessionSave();
        OC_LOG_WARN("[ProjectSessionAutosave] capture failed");
        return Result{
            .status = Status::CAPTURE_FAILED,
            .failureStage = FailureStage::CAPTURE,
            .error = ErrorCode::RESOURCE_EXHAUSTED,
            .errorContext = kCaptureStartFailed,
        };
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
        return Result{
            .status = Status::CAPTURE_FAILED,
            .failureStage = FailureStage::CAPTURE,
            .error = ErrorCode::RESOURCE_EXHAUSTED,
            .errorContext = kCaptureAdvanceFailed,
        };
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
        const auto error = started.error();
        capture_.cancel();
        state.requestProjectSessionSave();
        OC_LOG_WARN("[ProjectSessionAutosave] save start failed");
        return Result{
            .status = Status::SAVE_FAILED,
            .failureStage = FailureStage::PREPARE,
            .error = error.code,
            .errorContext = error.context,
        };
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
    ProjectSaveStage attemptedStage = ProjectSaveStage::PREPARE;
    auto saved = store_.advanceSaveCurrent(&attemptedStage);

    if (!saved) {
        const auto error = saved.error();
        state.requestProjectSessionSave();
        capture_.cancel();
        recovery_in_progress_ = false;
        OC_LOG_WARN("[ProjectSessionAutosave] save failed");
        return Result{
            .status = Status::SAVE_FAILED,
            .modifiedCounter = capturedToken.modifiedCounter,
            .failureStage = failureStage(attemptedStage),
            .error = error.code,
            .errorContext = error.context,
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

    if (!state.acknowledgeProjectSessionSave(capturedToken)) {
        cancelInFlight_();
        return Result{
            .status = Status::WAITING,
            .bytes = bytesWritten,
            .modifiedCounter = capturedToken.modifiedCounter,
            .failureStage = FailureStage::ACKNOWLEDGE,
            .error = ErrorCode::INVALID_STATE,
            .errorContext = kSaveAcknowledgementFailed,
        };
    }
    capture_.cancel();
    recovery_in_progress_ = false;
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
    recovery_in_progress_ = false;
}

bool ProjectSessionAutosaveService::inProgress_() const {
    return capture_.active() || store_.saveCurrentInProgress();
}

bool ProjectSessionAutosaveService::writeSessionActive() const {
    return store_.saveCurrentWriteSessionActive();
}

}  // namespace core::persistence

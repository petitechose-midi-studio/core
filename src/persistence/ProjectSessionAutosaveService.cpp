#include "persistence/ProjectSessionAutosaveService.hpp"

#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/log/Log.hpp>

#include "config/TimeCompat.hpp"
#include "diagnostics/StorageQualificationProbe.hpp"
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
const char kAutosaveJobBlocked[] PROGMEM =
    "project session autosave queue blocked";
const char kAutosaveJobAdvanceFailed[] PROGMEM =
    "project session autosave advance failed";
const char kAutosaveMeasurementFailed[] PROGMEM =
    "project session autosave measurement unavailable";
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

void recordAutosaveToken(
    const core::state::project::ProjectCaptureGuard* guard,
    core::diagnostics::storage_qualification::PhaseKind phase,
    ErrorCode result,
    ProjectSessionAutosaveService::FailureStage stage,
    bool recovery = false
) {
    if (guard == nullptr) return;
    const auto& token = guard->token;
    uint32_t flags = core::diagnostics::storage_qualification::saveTokenStageFlags(
        static_cast<uint8_t>(stage)
    );
    if (recovery) {
        flags |= core::diagnostics::storage_qualification::SaveTokenFlagRecovery;
    }
    core::diagnostics::storage_qualification::recordSaveToken(
        phase,
        token.session.bootGeneration,
        token.session.sessionEpoch,
        token.mutationEpoch,
        token.requestId,
        token.modifiedCounter,
        static_cast<uint8_t>(result),
        flags
    );
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
    uint32_t delayMs,
    MicrosProvider microsProvider
) : store_(store)
  , delay_ms_(delayMs == 0 ? core::state::CoreState::PROJECT_SESSION_AUTOSAVE_DELAY_MS
                           : delayMs)
  , micros_provider_(microsProvider ? microsProvider : &core::time_compat::micros) {
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
    cancelOrdinary_();
}

ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::update(
    core::state::CoreState& state,
    uint32_t nowMs,
    bool mutationPending,
    bool playbackActive
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
    return updatePending_(
        state,
        nowMs,
        mutationPending,
        playbackActive,
        inProgress
    );
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::updatePending_(
    core::state::CoreState& state,
    uint32_t nowMs,
    bool mutationPending,
    bool playbackActive,
    bool inProgress
) {
    if (inProgress) {
        auto& jobs = store_.productFiles().persistenceJobs();
        if (!job_token_.valid() || !jobs.owns(job_token_)) {
            cancelInFlight_();
            job_token_ = {};
            state.requestProjectSessionSave();
            inProgress = false;
        }
    }

    if (inProgress) {
        const bool transactionPending =
            mutationPending || state.hasPendingProjectTransaction();
        const auto* guard = capture_.guard();
        const bool captureStale = guard == nullptr ||
            !state.projectSessionSaveTokenMatches(guard->token);
        if (transactionPending || captureStale) {
            // Latch invalidation even during playback or while this job is
            // deferred. Durable unwind itself remains an admitted foreground
            // advance; no filesystem cleanup is allowed from this observer.
            requestOrdinaryCancel_(
                transactionPending ? Status::BLOCKED : Status::WAITING
            );
        }

        // An admitted save is frozen in place while music is running,
        // including an already-open write stream. A latched stale cleanup
        // resumes only after playback stops, so this branch performs no
        // filesystem work.
        if (playbackActive) {
            return Result{
                .status = Status::SAVING,
                .modifiedCounter = guard ? guard->token.modifiedCounter : 0U,
            };
        }

        if (inProgress_()) {
            return advanceOrdinary_(state, nowMs);
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

    if (playbackActive) {
        return Result{.status = Status::BLOCKED};
    }

    return startCapture_(state, nowMs);
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
    // flush() is a synchronous compatibility path used outside the firmware
    // foreground scheduler. Unwind any ordinary queued continuation before
    // driving the same capture/save primitives to completion locally.
    if (job_token_.valid()) cancelOrdinary_();
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
        if (!beginCapture_(state)) {
            state.requestProjectSessionSave();
            return Result{
                .status = Status::CAPTURE_FAILED,
                .failureStage = FailureStage::CAPTURE,
                .error = ErrorCode::RESOURCE_EXHAUSTED,
                .errorContext = kCaptureStartFailed,
            };
        }
        result = advanceCapture_(state);
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
    cancelOrdinary_();
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
        !beginCapture_(state)) {
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
    recordAutosaveToken(
        capture_.guard(),
        core::diagnostics::storage_qualification::PhaseKind::Admit,
        ErrorCode::OK,
        FailureStage::CAPTURE,
        true
    );
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
        recordAutosaveToken(
            guard,
            core::diagnostics::storage_qualification::PhaseKind::Cancel,
            ErrorCode::INVALID_STATE,
            FailureStage::CAPTURE,
            true
        );
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

FLASHMEM ProductPersistenceWorkQuota
ProjectSessionAutosaveService::recoveryWorkQuota() const {
    if (capture_.active()) {
        return capture_.nextSliceKind() ==
                       core::state::project::ProjectSnapshotCapture::SliceKind::SEQUENCER
            ? PRODUCT_PERSISTENCE_QUOTA_AUTOSAVE_SEQUENCER
            : PRODUCT_PERSISTENCE_QUOTA_AUTOSAVE_AUTOMATION;
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
    recordAutosaveToken(
        capture_.guard(),
        core::diagnostics::storage_qualification::PhaseKind::Advance,
        progress.status == core::state::project::ProjectSnapshotCapture::Status::FAILED ||
                progress.status == core::state::project::ProjectSnapshotCapture::Status::IDLE
            ? ErrorCode::RESOURCE_EXHAUSTED
            : (progress.status ==
                       core::state::project::ProjectSnapshotCapture::Status::STALE
                   ? ErrorCode::INVALID_STATE
                   : ErrorCode::OK),
        FailureStage::CAPTURE,
        true
    );
    if (progress.status ==
        core::state::project::ProjectSnapshotCapture::Status::STALE) {
        cancelInFlight_();
        return Result{
            .status = Status::WAITING,
            .modifiedCounter = progress.modifiedCounter,
            .workBytes = progress.workBytes,
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
            .workBytes = progress.workBytes,
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
            .workBytes = progress.workBytes,
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
            .workBytes = progress.workBytes,
        };
    }

    const uint32_t capturedCounter = guard->token.modifiedCounter;
    auto begun = store_.beginSaveCurrent(*snapshot_, recoveryLease);
    recordAutosaveToken(
        guard,
        core::diagnostics::storage_qualification::PhaseKind::Begin,
        begun ? ErrorCode::OK : begun.error().code,
        FailureStage::PREPARE,
        true
    );
    if (!begun) {
        const auto error = begun.error();
        cancelInFlight_();
        state.requestProjectSessionSave();
        return Result{
            .status = Status::SAVE_FAILED,
            .modifiedCounter = capturedCounter,
            .workBytes = progress.workBytes,
            .failureStage = FailureStage::PREPARE,
            .error = error.code,
            .errorContext = error.context,
        };
    }
    return Result{
        .status = Status::SAVING,
        .modifiedCounter = capturedCounter,
        .workBytes = progress.workBytes,
    };
}

FLASHMEM bool ProjectSessionAutosaveService::beginCapture_(
    core::state::CoreState& state
) {
    if (!snapshot_) {
        snapshot_ = core::state::project::makeProjectSnapshot();
    }
    const bool begun = snapshot_ && capture_.begin(state, *snapshot_);
    if (begun) {
        recordAutosaveToken(
            capture_.guard(),
            core::diagnostics::storage_qualification::PhaseKind::Begin,
            ErrorCode::OK,
            FailureStage::CAPTURE,
            recovery_in_progress_
        );
    }
    return begun;
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::startCapture_(
    core::state::CoreState& state,
    uint32_t nowMs
) {
    recovery_in_progress_ = false;
    ordinary_cancel_status_ = Status::IDLE;
    OC_PERF_SCOPE(perfStart, "persistence.autosave.start-capture");
    auto& jobs = store_.productFiles().persistenceJobs();
    if (jobs.depth() >= 2U) {
        return Result{
            .status = Status::BLOCKED,
            .error = ErrorCode::HARDWARE_BUSY,
            .errorContext = kAutosaveJobBlocked,
        };
    }
    if (!beginCapture_(state)) {
        state.requestProjectSessionSave();
        OC_LOG_WARN("[ProjectSessionAutosave] capture failed");
        return Result{
            .status = Status::CAPTURE_FAILED,
            .failureStage = FailureStage::CAPTURE,
            .error = ErrorCode::RESOURCE_EXHAUSTED,
            .errorContext = kCaptureStartFailed,
        };
    }

    const auto* qualificationGuard = capture_.guard();
    core::diagnostics::storage_qualification::setRequestId(
        qualificationGuard ? qualificationGuard->token.requestId : 0U
    );
    auto admitted = jobs.admit({
        .owner = ProductPersistenceJobOwner::PROJECT_AUTOSAVE,
        .nowMs = nowMs,
        .deadlineAfterMs = 0U,
        .quota = nextWorkQuota_(),
    });
    core::diagnostics::storage_qualification::clearRequestId();
    if (!admitted) {
        const auto error = admitted.error();
        capture_.cancel();
        return Result{
            .status = Status::BLOCKED,
            .error = error.code,
            .errorContext = error.context,
        };
    }
    job_token_ = std::move(admitted.value());
    return jobs.isActive(job_token_)
        ? advanceOrdinary_(state, nowMs)
        : Result{
              .status = Status::SAVING,
              .modifiedCounter = capture_.guard()->token.modifiedCounter,
          };
}

FLASHMEM ProjectSessionAutosaveService::Result
ProjectSessionAutosaveService::advanceOrdinary_(
    core::state::CoreState& state,
    uint32_t nowMs
) {
    auto& files = store_.productFiles();
    auto& jobs = files.persistenceJobs();
    if (!job_token_.valid() || !jobs.owns(job_token_)) {
        cancelInFlight_();
        job_token_ = {};
        state.requestProjectSessionSave();
        return Result{
            .status = Status::WAITING,
            .error = ErrorCode::INVALID_STATE,
            .errorContext = kAutosaveJobAdvanceFailed,
        };
    }
    if (!jobs.isActive(job_token_)) {
        const auto* guard = capture_.guard();
        return Result{
            .status = ordinaryCancelPending_()
                ? ordinary_cancel_status_
                : Status::SAVING,
            .modifiedCounter = guard ? guard->token.modifiedCounter : 0U,
        };
    }

    auto prepared = jobs.prepareAdvance(job_token_, nextWorkQuota_());
    if (!prepared) {
        if (prepared.error().code == ErrorCode::RESOURCE_EXHAUSTED) {
            cancelOrdinary_();
            state.requestProjectSessionSave();
            return Result{
                .status = Status::SAVE_FAILED,
                .error = prepared.error().code,
                .errorContext = prepared.error().context,
            };
        }
        // Another owner already consumed this foreground turn. A pending
        // invalidation remains latched; it must not unwind outside its own
        // measured advance.
        return Result{
            .status = ordinaryCancelPending_()
                ? ordinary_cancel_status_
                : Status::SAVING,
        };
    }
    const auto* qualificationGuard = capture_.guard();
    core::diagnostics::storage_qualification::setRequestId(
        qualificationGuard ? qualificationGuard->token.requestId : 0U
    );
    auto claimed = jobs.claimAdvance(job_token_, nowMs);
    if (!claimed) {
        core::diagnostics::storage_qualification::clearRequestId();
        return Result{.status = Status::SAVING};
    }

    ProductPersistenceWorkUsage usage{};
    const uint32_t startedMicros = micros_provider_();
    Result result{
        .status = Status::SAVE_FAILED,
        .error = ErrorCode::INVALID_STATE,
        .errorContext = kSaveProgressLost,
    };
    auto measured = files.measurePersistenceWork(usage);
    if (!measured) {
        result.error = measured.error().code;
        result.errorContext = kAutosaveMeasurementFailed;
    } else {
        auto measurement = std::move(measured.value());
        if (ordinaryCancelPending_()) {
            const auto* guard = capture_.guard();
            const uint32_t modifiedCounter =
                guard ? guard->token.modifiedCounter : 0U;
            const Status cancelStatus = ordinary_cancel_status_;
            recordAutosaveToken(
                guard,
                core::diagnostics::storage_qualification::PhaseKind::Cancel,
                ErrorCode::INVALID_STATE,
                FailureStage::CAPTURE
            );
            cancelInFlight_();
            result = Result{
                .status = cancelStatus,
                .modifiedCounter = modifiedCounter,
            };
        } else if (capture_.active()) {
            result = advanceCapture_(state);
        } else if (store_.saveCurrentInProgress()) {
            result = advanceSave_(state);
        }
    }

    const uint32_t byteRoom =
        std::numeric_limits<uint32_t>::max() - usage.bytes;
    usage.bytes += result.workBytes > byteRoom ? byteRoom : result.workBytes;
    usage.wallMicros = static_cast<uint32_t>(micros_provider_() - startedMicros);
    const bool safeYield = !store_.saveCurrentWriteSessionActive();
    auto finished = jobs.finishAdvance(job_token_, usage, safeYield);
    if (!finished) {
        const auto error = finished.error();
        cancelInFlight_();
        (void)jobs.cancelAfterUnwind(job_token_);
        state.requestProjectSessionSave();
        return Result{
            .status = Status::SAVE_FAILED,
            .failureStage = result.failureStage,
            .error = error.code,
            .errorContext = error.context,
        };
    }

    if (result.status == Status::SAVED) {
        if (!jobs.complete(job_token_)) {
            (void)jobs.cancelAfterUnwind(job_token_);
        }
        ordinary_cancel_status_ = Status::IDLE;
    } else if (result.status != Status::SAVING) {
        cancelInFlight_();
        (void)jobs.cancelAfterUnwind(job_token_);
        ordinary_cancel_status_ = Status::IDLE;
    }
    return result;
}

FLASHMEM ProjectSessionAutosaveService::Result ProjectSessionAutosaveService::advanceCapture_(
    core::state::CoreState& state
) {
    OC_PERF_SCOPE(perfCapture, "persistence.autosave.capture-slice");
    const auto progress = capture_.advance();
    recordAutosaveToken(
        capture_.guard(),
        core::diagnostics::storage_qualification::PhaseKind::Advance,
        progress.status == core::state::project::ProjectSnapshotCapture::Status::FAILED ||
                progress.status == core::state::project::ProjectSnapshotCapture::Status::IDLE
            ? ErrorCode::RESOURCE_EXHAUSTED
            : (progress.status ==
                       core::state::project::ProjectSnapshotCapture::Status::STALE
                   ? ErrorCode::INVALID_STATE
                   : ErrorCode::OK),
        FailureStage::CAPTURE
    );

    if (progress.status == core::state::project::ProjectSnapshotCapture::Status::STALE) {
        return Result{
            .status = Status::WAITING,
            .modifiedCounter = progress.modifiedCounter,
            .workBytes = progress.workBytes,
        };
    }
    if (progress.status == core::state::project::ProjectSnapshotCapture::Status::FAILED ||
        progress.status == core::state::project::ProjectSnapshotCapture::Status::IDLE) {
        state.requestProjectSessionSave();
        OC_LOG_WARN("[ProjectSessionAutosave] capture failed");
        return Result{
            .status = Status::CAPTURE_FAILED,
            .modifiedCounter = progress.modifiedCounter,
            .workBytes = progress.workBytes,
            .failureStage = FailureStage::CAPTURE,
            .error = ErrorCode::RESOURCE_EXHAUSTED,
            .errorContext = kCaptureAdvanceFailed,
        };
    }
    if (progress.status == core::state::project::ProjectSnapshotCapture::Status::IN_PROGRESS) {
        return Result{
            .status = Status::SAVING,
            .modifiedCounter = progress.modifiedCounter,
            .workBytes = progress.workBytes,
        };
    }

    const auto* guard = capture_.guard();
    if (guard == nullptr || !capture_.complete() ||
        !state.projectSessionSaveTokenMatches(guard->token)) {
        cancelInFlight_();
        return Result{
            .status = Status::WAITING,
            .modifiedCounter = progress.modifiedCounter,
            .workBytes = progress.workBytes,
        };
    }
    auto started = store_.beginSaveCurrent(*snapshot_);
    recordAutosaveToken(
        guard,
        core::diagnostics::storage_qualification::PhaseKind::Begin,
        started ? ErrorCode::OK : started.error().code,
        FailureStage::PREPARE
    );
    if (!started) {
        const auto error = started.error();
        capture_.cancel();
        state.requestProjectSessionSave();
        OC_LOG_WARN("[ProjectSessionAutosave] save start failed");
        return Result{
            .status = Status::SAVE_FAILED,
            .modifiedCounter = progress.modifiedCounter,
            .workBytes = progress.workBytes,
            .failureStage = FailureStage::PREPARE,
            .error = error.code,
            .errorContext = error.context,
        };
    }
    return Result{
        .status = Status::SAVING,
        .modifiedCounter = guard->token.modifiedCounter,
        .workBytes = progress.workBytes,
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
        recordAutosaveToken(
            guard,
            core::diagnostics::storage_qualification::PhaseKind::Cancel,
            ErrorCode::INVALID_STATE,
            FailureStage::ACKNOWLEDGE,
            recovery_in_progress_
        );
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
        recordAutosaveToken(
            guard,
            core::diagnostics::storage_qualification::PhaseKind::Advance,
            error.code,
            failureStage(attemptedStage),
            recovery_in_progress_
        );
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
    recordAutosaveToken(
        guard,
        saved.value().complete
            ? core::diagnostics::storage_qualification::PhaseKind::End
            : core::diagnostics::storage_qualification::PhaseKind::Advance,
        ErrorCode::OK,
        failureStage(attemptedStage),
        recovery_in_progress_
    );
    if (!saved.value().complete) {
        return Result{
            .status = Status::SAVING,
            .modifiedCounter = capturedToken.modifiedCounter,
            .workBytes = saved.value().workBytes,
        };
    }

    const uint32_t bytesWritten = saved.value().bytesWritten;
    OC_PERF_UNITS(perfSave, bytesWritten, 1U);

    if (!state.acknowledgeProjectSessionSave(capturedToken)) {
        recordAutosaveToken(
            guard,
            core::diagnostics::storage_qualification::PhaseKind::Cancel,
            ErrorCode::INVALID_STATE,
            FailureStage::ACKNOWLEDGE,
            recovery_in_progress_
        );
        cancelInFlight_();
        return Result{
            .status = Status::WAITING,
            .bytes = bytesWritten,
            .modifiedCounter = capturedToken.modifiedCounter,
            .workBytes = saved.value().workBytes,
            .failureStage = FailureStage::ACKNOWLEDGE,
            .error = ErrorCode::INVALID_STATE,
            .errorContext = kSaveAcknowledgementFailed,
        };
    }
    recordAutosaveToken(
        guard,
        core::diagnostics::storage_qualification::PhaseKind::Complete,
        ErrorCode::OK,
        FailureStage::ACKNOWLEDGE,
        recovery_in_progress_
    );
    capture_.cancel();
    recovery_in_progress_ = false;
    return Result{
        .status = Status::SAVED,
        .bytes = bytesWritten,
        .modifiedCounter = capturedToken.modifiedCounter,
        .workBytes = saved.value().workBytes,
    };
}

FLASHMEM ProductPersistenceWorkQuota
ProjectSessionAutosaveService::nextWorkQuota_() const {
    if (ordinaryCancelPending_()) {
        return PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE;
    }
    return recoveryWorkQuota();
}

FLASHMEM void ProjectSessionAutosaveService::requestOrdinaryCancel_(Status status) {
    if (status != Status::BLOCKED && status != Status::WAITING) return;
    if (ordinary_cancel_status_ == Status::IDLE || status == Status::BLOCKED) {
        ordinary_cancel_status_ = status;
    }
}

FLASHMEM bool ProjectSessionAutosaveService::ordinaryCancelPending_() const {
    return ordinary_cancel_status_ == Status::BLOCKED ||
           ordinary_cancel_status_ == Status::WAITING;
}

FLASHMEM void ProjectSessionAutosaveService::cancelInFlight_() {
    capture_.cancel();
    if (store_.saveCurrentInProgress()) {
        store_.cancelSaveCurrent();
    }
    recovery_in_progress_ = false;
}

FLASHMEM void ProjectSessionAutosaveService::cancelOrdinary_() {
    cancelInFlight_();
    ordinary_cancel_status_ = Status::IDLE;
    if (!job_token_.valid()) return;

    auto& jobs = store_.productFiles().persistenceJobs();
    if (!jobs.owns(job_token_)) {
        job_token_ = {};
        return;
    }
    if (!jobs.cancel(job_token_)) {
        (void)jobs.cancelAfterUnwind(job_token_);
    }
}

bool ProjectSessionAutosaveService::inProgress_() const {
    return capture_.active() || store_.saveCurrentInProgress();
}

FLASHMEM bool ProjectSessionAutosaveService::inspectPersistenceJob(
    ProductPersistenceJobSnapshot& snapshot
) const {
    return job_token_.valid() &&
           store_.productFiles().persistenceJobs().inspect(job_token_, snapshot);
}

}  // namespace core::persistence

#include "state/project/ProjectSnapshot.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/project/ProjectDomainRules.hpp"
#include "state/project/ProjectMenuModel.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::project {

namespace {

FLASHMEM ProjectState projectStateFromRuntime(
    const core::state::CoreState& state,
    const ProjectTrackSnapshot&
) {
    ProjectState project = state.project;

    project.transport.tempoBpm = sanitizeProjectTempoBpm(state.statusBar.tempo.get());
    project.transport.swingPercent =
        sanitizeProjectSwingPercent(state.projectNavigation.transportSwingPercent);
    project.transport.runMode = sanitizeProjectRunMode(state.projectNavigation.transportRunMode);

    project.musical.scale = state.sequencerTracks.projectScaleSettings();
    project.musical.scale.clamp();
    project.musical.patternsInheritScale = state.projectNavigation.patternsInheritScale;
    project.musical.clipsInheritScale = state.projectNavigation.clipsInheritScale;

    project.editing.stepPasteMode =
        sanitizeProjectStepPasteMode(state.projectNavigation.stepPasteMode);
    for (uint8_t lane = 0; lane < PROJECT_CC_LANE_DEFAULT_COUNT; ++lane) {
        project.editing.ccLaneDefaultControllers[lane] =
            sanitizeProjectCcLaneDefault(
                state.projectNavigation.ccLaneDefaultControllers[lane],
                lane
            );
    }

    return project;
}

FLASHMEM void applyProjectTransport(core::state::CoreState& state,
                                    const ProjectTransportState& transport) {
    const float tempo = sanitizeProjectTempoBpm(transport.tempoBpm);
    state.statusBar.tempo.set(tempo);
    if (!state.statusBar.tempoLocked.get()) {
        state.statusBar.tempoDisplay.set(tempo);
    }

    state.projectNavigation.transportSwingPercent =
        sanitizeProjectSwingPercent(transport.swingPercent);
    state.projectNavigation.transportRunMode = sanitizeProjectRunMode(transport.runMode);
}

FLASHMEM void applyProjectMusicalContext(core::state::CoreState& state,
                                         ProjectMusicalContext musical) {
    musical.scale.clamp();
    state.sequencerTracks.setProjectScaleSettings(musical.scale);
    state.projectNavigation.patternsInheritScale = musical.patternsInheritScale;
    state.projectNavigation.clipsInheritScale = musical.clipsInheritScale;
}

FLASHMEM void applyProjectEditing(core::state::CoreState& state,
                                  ProjectEditingState editing) {
    state.projectNavigation.stepPasteMode =
        sanitizeProjectStepPasteMode(editing.stepPasteMode);
    for (uint8_t lane = 0; lane < PROJECT_CC_LANE_DEFAULT_COUNT; ++lane) {
        state.projectNavigation.ccLaneDefaultControllers[lane] =
            sanitizeProjectCcLaneDefault(
                editing.ccLaneDefaultControllers[lane],
                lane
            );
    }
}

}  // namespace

FLASHMEM bool ProjectSnapshotCapture::begin(const core::state::CoreState& state,
                                            ProjectSnapshot& snapshot) {
    cancel();
    if (!snapshot.projectControl ||
        state.macroHistory.hasPendingModulatorAuditionTransaction(state.pages) ||
        state.projectTrackHistory.hasPendingGesture()) {
        return false;
    }

    state_ = &state;
    snapshot_ = &snapshot;
    guard_ = {
        .token = state.projectSessionSaveToken(),
        .authoredRevision = state.pages.control.authoredRevision,
        .projectTrackRevision = state.projectTracks.revision.get(),
    };
    phase_ = Phase::PROJECT;
    return true;
}

FLASHMEM ProjectSnapshotCapture::Progress ProjectSnapshotCapture::advance() {
    if (!active() || state_ == nullptr || snapshot_ == nullptr) {
        return {.status = Status::IDLE};
    }

    if (state_->macroHistory.hasPendingModulatorAuditionTransaction(state_->pages) ||
        state_->projectTrackHistory.hasPendingGesture() ||
        !state_->projectSessionSaveTokenMatches(guard_.token) ||
        state_->pages.control.authoredRevision != guard_.authoredRevision ||
        state_->projectTracks.revision.get() != guard_.projectTrackRevision) {
        const uint32_t modifiedCounter = guard_.token.modifiedCounter;
        cancel();
        return {.status = Status::STALE, .modifiedCounter = modifiedCounter};
    }

    OC_PERF_SCOPE(perfCaptureSlice, "persistence.project-snapshot.capture-slice");
    OC_PERF_UNITS(perfCaptureSlice, static_cast<uint32_t>(phase_), 0);

    switch (phase_) {
        case Phase::PROJECT:
            captureProjectTrackSnapshot(state_->projectTracks, snapshot_->projectTracks);
            snapshot_->project = projectStateFromRuntime(*state_, snapshot_->projectTracks);
            phase_ = Phase::MACROS;
            break;

        case Phase::MACROS:
            snapshot_->macroTracks = state_->pages.tracks;
            state_->pages.captureSharedTrackState(
                snapshot_->sharedTrackEnabledMask,
                snapshot_->sharedTrackActive
            );
            phase_ = Phase::AUTOMATION;
            break;

        case Phase::AUTOMATION:
            *snapshot_->projectControl = state_->pages.control.authored;
            phase_ = Phase::SEQUENCER;
            break;

        case Phase::SEQUENCER:
            if (!core::state::sequencer::captureHistorySnapshot(
                    state_->sequencerTracks,
                    state_->sequencer,
                    snapshot_->sequencer
                )) {
                const uint32_t modifiedCounter = guard_.token.modifiedCounter;
                cancel();
                return {.status = Status::FAILED, .modifiedCounter = modifiedCounter};
            }
            phase_ = Phase::COMPLETE;
            break;

        case Phase::COMPLETE:
        case Phase::IDLE:
            return {.status = Status::IDLE};
    }

    if (state_->macroHistory.hasPendingModulatorAuditionTransaction(state_->pages) ||
        !state_->projectSessionSaveTokenMatches(guard_.token) ||
        state_->pages.control.authoredRevision != guard_.authoredRevision ||
        state_->projectTracks.revision.get() != guard_.projectTrackRevision) {
        const uint32_t modifiedCounter = guard_.token.modifiedCounter;
        cancel();
        return {.status = Status::STALE, .modifiedCounter = modifiedCounter};
    }

    if (phase_ != Phase::COMPLETE) {
        return {.status = Status::IN_PROGRESS,
                .modifiedCounter = guard_.token.modifiedCounter};
    }

    return {.status = Status::COMPLETE,
            .modifiedCounter = guard_.token.modifiedCounter};
}

FLASHMEM void ProjectSnapshotCapture::cancel() {
    state_ = nullptr;
    snapshot_ = nullptr;
    phase_ = Phase::IDLE;
    guard_ = {};
}

FLASHMEM bool ProjectSnapshotCapture::active() const {
    return phase_ != Phase::IDLE && phase_ != Phase::COMPLETE;
}

FLASHMEM bool ProjectSnapshotCapture::complete() const {
    return phase_ == Phase::COMPLETE;
}

FLASHMEM const ProjectCaptureGuard* ProjectSnapshotCapture::guard() const {
    return phase_ == Phase::IDLE ? nullptr : &guard_;
}

namespace {

FLASHMEM bool captureToCompletion(const core::state::CoreState& state,
                                  ProjectSnapshot& snapshot) {
    ProjectSnapshotCapture capture;
    if (!capture.begin(state, snapshot)) return false;

    while (capture.active()) {
        const auto progress = capture.advance();
        if (progress.status == ProjectSnapshotCapture::Status::COMPLETE) return true;
        if (progress.status == ProjectSnapshotCapture::Status::FAILED ||
            progress.status == ProjectSnapshotCapture::Status::STALE) {
            return false;
        }
    }
    return false;
}

}  // namespace

FLASHMEM ProjectSnapshotPtr captureProjectSnapshotOwned(const core::state::CoreState& state) {
    OC_PERF_SCOPE(perfCapture, "persistence.project-snapshot.allocate-and-capture");
    auto snapshot = makeProjectSnapshot();
    if (!snapshot || !captureToCompletion(state, *snapshot)) return {};
    return snapshot;
}

FLASHMEM bool captureProjectSnapshot(const core::state::CoreState& state, ProjectSnapshot& out) {
    return captureToCompletion(state, out);
}

FLASHMEM bool applyProjectSnapshot(core::state::CoreState& state,
                                   const ProjectSnapshot& snapshot) {
    if (state.sequencer.stepContentDraft.active.get()) {
        state.sequencer.stepContentDraft.noteBlockedTransition(
            core::state::sequencer::
                SequencerStepContentDraftBlockedTransition::PROJECT_LOAD
        );
        return false;
    }
    if (state.macroHistory.hasPendingModulatorAuditionTransaction(state.pages) ||
        !snapshot.projectControl ||
        !validProjectTrackSnapshot(snapshot.projectTracks) ||
        !core::state::modulation::validProjectModulationDomain(
            snapshot.projectControl->modulation,
            snapshot.projectControl->curves,
            &snapshot.projectControl->automation
        )) {
        return false;
    }
    if (!state.macroHistory.abortPendingModulatorAudition(state.pages)) {
        return false;
    }
    if (!core::state::sequencer::applyHistorySnapshot(
            state.sequencerTracks,
            state.sequencer,
            snapshot.sequencer
        )) {
        return false;
    }

    state.project = snapshot.project;
    applyProjectTransport(state, state.project.transport);
    applyProjectMusicalContext(state, state.project.musical);
    applyProjectEditing(state, state.project.editing);

    state.pages.restoreTracksWithSharedState(
        snapshot.macroTracks,
        snapshot.sharedTrackEnabledMask,
        snapshot.sharedTrackActive
    );
    state.pages.control.authored = *snapshot.projectControl;
    state.pages.control.plan = {};
    state.pages.control.runtime = {};
    state.pages.control.timeTelemetry = {};
    state.pages.control.sourceScratch.fill(0.0f);
    state.pages.control.triggerScratch = {};
    state.pages.control.audition = {};
    state.pages.control.focus = {};
    state.pages.control.compiledRevision = 0U;
    state.pages.control.runtimeContextHash = 0U;
    state.pages.control.reserved = 0U;
    state.pages.control.markAuthoredMutation();
    if (applyProjectTrackSnapshot(state.projectTracks, snapshot.projectTracks).status ==
        ProjectTrackMutationStatus::INVALID_SNAPSHOT) {
        return false;
    }
    state.pages.syncSharedTrackState(
        state.sequencerTracks.currentEnabledMask(),
        state.sequencerTracks.activeTrackIndex()
    );
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sharedTrackEnabledMask.set(state.sequencerTracks.currentEnabledMask());
    state.sharedTrackActive.set(state.sequencerTracks.activeTrackIndex());
    state.clearPendingSequencerApply();
    if (!state.clearProjectHistory()) return false;
    core::state::project::reconcileProjectModulatorNavigationAfterHistory(
        state.projectNavigation,
        state.pages.control.authored.modulation,
        false
    );
    // Manual is Project-scoped runtime intent: it survives navigation and UI
    // teardown, but never crosses a load boundary or enters persistence.
    state.macroUi.resetInteraction();
    state.macroUi.resetProjectRuntime();
    state.requestMacroRuntimeOwnerActivation();
    state.requestSequencerRuntimeProjectReset();
    if (!state.advanceProjectSessionIdentity_()) return false;
    return true;
}

}  // namespace core::state::project

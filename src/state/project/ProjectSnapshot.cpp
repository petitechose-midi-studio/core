#include "state/project/ProjectSnapshot.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"
#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::state::project {

namespace {

FLASHMEM ProjectState projectStateFromRuntime(const core::state::CoreState& state) {
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

    const uint8_t activeTrack = state.sequencerTracks.activeTrackIndex();
    for (uint8_t i = 0; i < project.routing.outputMidiChannels.size(); ++i) {
        const uint8_t channel = (i == activeTrack)
            ? state.sequencer.pattern.midiChannel.get()
            : state.sequencerTracks.track(i).midiChannel.get();
        project.routing.outputMidiChannels[i] = sanitizeProjectMidiChannel(channel);
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
}

FLASHMEM void applyProjectRouting(core::state::CoreState& state,
                                  const ProjectRoutingState& routing) {
    for (uint8_t i = 0; i < routing.outputMidiChannels.size(); ++i) {
        state.sequencerTracks.track(i).midiChannel.set(
            sanitizeProjectMidiChannel(routing.outputMidiChannels[i])
        );
    }

    const uint8_t activeTrack = state.sequencerTracks.activeTrackIndex();
    state.sequencer.pattern.midiChannel.set(
        sanitizeProjectMidiChannel(routing.outputMidiChannels[activeTrack])
    );
}

}  // namespace

FLASHMEM bool ProjectSnapshotCapture::begin(const core::state::CoreState& state,
                                            ProjectSnapshot& snapshot) {
    cancel();
    if (!snapshot.macroAutomation) return false;

    state_ = &state;
    snapshot_ = &snapshot;
    modified_counter_ = state.project.metadata.modifiedCounter;
    phase_ = Phase::PROJECT;
    return true;
}

FLASHMEM ProjectSnapshotCapture::Progress ProjectSnapshotCapture::advance() {
    if (!active() || state_ == nullptr || snapshot_ == nullptr) {
        return {.status = Status::IDLE};
    }

    if (state_->project.metadata.modifiedCounter != modified_counter_) {
        const uint32_t modifiedCounter = modified_counter_;
        cancel();
        return {.status = Status::STALE, .modifiedCounter = modifiedCounter};
    }

    OC_PERF_SCOPE(perfCaptureSlice, "persistence.project-snapshot.capture-slice");
    OC_PERF_UNITS(perfCaptureSlice, static_cast<uint32_t>(phase_), 0);

    switch (phase_) {
        case Phase::PROJECT:
            snapshot_->project = projectStateFromRuntime(*state_);
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
            *snapshot_->macroAutomation = state_->pages.automation;
            phase_ = Phase::SEQUENCER;
            break;

        case Phase::SEQUENCER:
            if (!core::state::sequencer::captureHistorySnapshot(
                    state_->sequencerTracks,
                    state_->sequencer,
                    snapshot_->sequencer
                )) {
                const uint32_t modifiedCounter = modified_counter_;
                cancel();
                return {.status = Status::FAILED, .modifiedCounter = modifiedCounter};
            }
            phase_ = Phase::COMPLETE;
            break;

        case Phase::COMPLETE:
        case Phase::IDLE:
            return {.status = Status::IDLE};
    }

    if (state_->project.metadata.modifiedCounter != modified_counter_) {
        const uint32_t modifiedCounter = modified_counter_;
        cancel();
        return {.status = Status::STALE, .modifiedCounter = modifiedCounter};
    }

    if (phase_ != Phase::COMPLETE) {
        return {.status = Status::IN_PROGRESS, .modifiedCounter = modified_counter_};
    }

    const uint32_t modifiedCounter = modified_counter_;
    cancel();
    return {.status = Status::COMPLETE, .modifiedCounter = modifiedCounter};
}

FLASHMEM void ProjectSnapshotCapture::cancel() {
    state_ = nullptr;
    snapshot_ = nullptr;
    phase_ = Phase::IDLE;
    modified_counter_ = 0;
}

FLASHMEM bool ProjectSnapshotCapture::active() const {
    return phase_ != Phase::IDLE;
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
    if (!snapshot.macroAutomation) return false;
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
    applyProjectRouting(state, state.project.routing);

    state.pages.restoreTracksWithSharedState(
        snapshot.macroTracks,
        snapshot.sharedTrackEnabledMask,
        snapshot.sharedTrackActive
    );
    state.pages.automation = *snapshot.macroAutomation;
    core::state::macro::macroAutomationCompactPool(state.pages.automation);
    state.pages.syncSharedTrackState(
        state.sequencerTracks.currentEnabledMask(),
        state.sequencerTracks.activeTrackIndex()
    );
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sharedTrackEnabledMask.set(state.sequencerTracks.currentEnabledMask());
    state.sharedTrackActive.set(state.sequencerTracks.activeTrackIndex());
    state.projectNavigation.notifyContentChanged();
    state.clearPendingSequencerApply();
    state.clearSequencerHistory();
    state.statusBar.pageName.set(state.pages.activePageData().name);
    // Manual is Project-scoped runtime intent: it survives navigation and UI
    // teardown, but never crosses a load boundary or enters persistence.
    state.macroUi.resetInteraction();
    state.macroUi.resetProjectRuntime();
    return true;
}

}  // namespace core::state::project

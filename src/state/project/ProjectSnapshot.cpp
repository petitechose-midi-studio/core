#include "state/project/ProjectSnapshot.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

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

ProjectSnapshot::ProjectSnapshot() = default;
ProjectSnapshot::~ProjectSnapshot() = default;
ProjectSnapshot::ProjectSnapshot(ProjectSnapshot&&) noexcept = default;
ProjectSnapshot& ProjectSnapshot::operator=(ProjectSnapshot&&) noexcept = default;

FLASHMEM bool captureProjectSnapshot(const core::state::CoreState& state, ProjectSnapshot& out) {
    ProjectSnapshot next;
    next.project = projectStateFromRuntime(state);
    next.macroTracks = state.pages.tracks;
    state.pages.captureSharedTrackState(next.sharedTrackEnabledMask, next.sharedTrackActive);

    if (!core::state::sequencer::captureHistorySnapshot(
            state.sequencerTracks,
            state.sequencer,
            next.sequencer
        )) {
        return false;
    }

    out = std::move(next);
    return true;
}

FLASHMEM bool applyProjectSnapshot(core::state::CoreState& state,
                                   const ProjectSnapshot& snapshot) {
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
    applyProjectRouting(state, state.project.routing);

    state.pages.restoreTracksWithSharedState(
        snapshot.macroTracks,
        snapshot.sharedTrackEnabledMask,
        snapshot.sharedTrackActive
    );
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
    return true;
}

}  // namespace core::state::project

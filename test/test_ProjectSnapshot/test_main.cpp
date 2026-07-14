#include <cassert>
#include <cstring>
#include <iostream>

#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../../src/state/sequencer/SequencerScaleState.hpp"
#include "../support/CoreStorages.hpp"

namespace {

namespace project = core::state::project;
namespace sequencer = core::state::sequencer;

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleSettings;
using oc::note::sequencer::StepSequencerScaleType;

core::state::CoreState makeCoreState(test_support::CoreStorages& storages) {
    return core::state::CoreState{
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary,
    };
}

bool sameScale(const StepSequencerScaleSettings& lhs,
               const StepSequencerScaleSettings& rhs) {
    return lhs.root == rhs.root && lhs.type == rhs.type && lhs.mode == rhs.mode;
}

void test_project_state_defaults_are_stable() {
    project::ProjectState state;

    assert(state.metadata.id[0] == '\0');
    assert(std::strcmp(state.metadata.name.data(), "untitled") == 0);
    assert(!state.metadata.dirty);
    assert(!state.metadata.hasSavedIdentity);
    assert(state.transport.tempoBpm == 120.0f);
    assert(state.transport.swingPercent == 0);
    assert(state.transport.runMode == 0);
    assert(sameScale(state.musical.scale, sequencer::defaultProjectScaleSettings()));
    assert(state.musical.patternsInheritScale);
    assert(state.musical.clipsInheritScale);
    assert(state.editing.stepPasteMode == project::PROJECT_STEP_PASTE_MODE_DEFAULT);

    for (uint8_t i = 0; i < state.routing.outputMidiChannels.size(); ++i) {
        assert(state.routing.outputMidiChannels[i] == i);
    }

    std::cout << "[PASS] test_project_state_defaults_are_stable\n";
}

void test_snapshot_macro_tracks_keep_track_specific_defaults() {
    project::ProjectSnapshot snapshot;
    for (uint8_t i = 0; i < snapshot.macroTracks.size(); ++i) {
        assert(snapshot.macroTracks[i].channel == i);
    }

    std::cout << "[PASS] test_snapshot_macro_tracks_keep_track_specific_defaults\n";
}

void configureProjectSession(core::state::CoreState& state) {
    std::strncpy(
        state.project.metadata.id.data(),
        "p123",
        state.project.metadata.id.size() - 1
    );
    std::strncpy(
        state.project.metadata.name.data(),
        "p123",
        state.project.metadata.name.size() - 1
    );
    state.project.metadata.modifiedCounter = 42;
    state.project.metadata.dirty = true;
    state.project.metadata.hasSavedIdentity = true;

    state.statusBar.tempo.set(137.0f);
    state.statusBar.tempoDisplay.set(137.0f);
    state.projectNavigation.transportSwingPercent = 23;
    state.projectNavigation.transportRunMode = 2;
    state.projectNavigation.patternsInheritScale = false;
    state.projectNavigation.clipsInheritScale = true;
    state.projectNavigation.stepPasteMode = project::ProjectStepPasteMode::PAGE;

    StepSequencerScaleSettings scale{
        .root = 2,
        .type = StepSequencerScaleType::WholeTone,
        .mode = StepSequencerScaleConstraintMode::ConstrainDown,
    };
    assert(state.sequencerTracks.setProjectScaleSettings(scale));

    assert(state.setSharedTrackState(0x0003, 1));
    auto& page = state.pages.activePageData();
    std::strncpy(page.name, "Snapshot", sizeof(page.name) - 1);
    page.setMacroActive(0, true);
    page.cc[0] = 74;
    page.values[0] = 0.75f;

    const auto automationAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    auto* automationSlot = core::state::macro::macroAutomationGetOrCreateSlot(
        state.pages.automation,
        automationAddress
    );
    assert(automationSlot != nullptr);
    core::state::macro::MacroAutomationLane automation;
    assert(core::state::macro::macroAutomationAppendPoint(automation, 0.0f, 0.2f));
    assert(core::state::macro::macroAutomationAppendPoint(automation, 2.0f, 0.8f));
    assert(core::state::macro::macroAutomationAssignAutomation(
        state.pages.automation,
        *automationSlot,
        automation
    ));
    automationSlot->automation.playbackState =
        core::state::macro::MacroCurvePlaybackState::OFF;

    core::state::macro::MacroModulationShape modulation;
    assert(core::state::macro::macroModulationAppendPoint(modulation, 0.0f, -0.4f));
    assert(core::state::macro::macroModulationAppendPoint(modulation, 2.0f, 0.3f));
    assert(core::state::macro::macroAutomationAssignModulation(
        state.pages.automation,
        *automationSlot,
        modulation
    ));
    automationSlot->modulation.playbackState =
        core::state::macro::MacroCurvePlaybackState::SUSPENDED_AFTER_RECORD;
    automationSlot->modulation.modulationOrigin =
        core::state::macro::MacroModulationOrigin::CONVERTED_MEAN;
    automationSlot->modulationDepth = 0.37f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.length.set(15);
    state.sequencer.pattern.stepsPerBeat.set(6);
    state.sequencer.pattern.midiChannel.set(9);
    state.sequencer.setStepDataAt(0, 66, 111, 88);
    state.sequencer.pattern.toggle(0);
    state.sequencer.focusedStep.set(0);
    state.sequencer.page.set(0);
}

void test_snapshot_capture_apply_restores_project_session() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));

    const auto capturedProjectAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    assert(state.macroUi.manualOverrides.activate(capturedProjectAddress, 0.91f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);

    state.resetMusicalProject();
    assert(state.statusBar.tempo.get() == 120.0f);
    assert(state.sequencer.pattern.length.get() == sequencer::SequencerPatternState::DEFAULT_LENGTH);
    assert(state.sharedTrackActive.get() == 0);
    assert(state.macroUi.manualOverrides.entryCount == 0);

    const auto resetProjectAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    assert(state.macroUi.manualOverrides.activate(resetProjectAddress, 0.13f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);

    assert(project::applyProjectSnapshot(state, snapshot));
    assert(state.macroUi.manualOverrides.entryCount == 0);

    assert(std::strcmp(state.project.metadata.id.data(), "p123") == 0);
    assert(std::strcmp(state.project.metadata.name.data(), "p123") == 0);
    assert(state.project.metadata.modifiedCounter == 42);
    assert(state.project.metadata.dirty);
    assert(state.project.metadata.hasSavedIdentity);

    assert(state.statusBar.tempo.get() == 137.0f);
    assert(state.statusBar.tempoDisplay.get() == 137.0f);
    assert(state.projectNavigation.transportSwingPercent == 23);
    assert(state.projectNavigation.transportRunMode == 2);
    assert(!state.projectNavigation.patternsInheritScale);
    assert(state.projectNavigation.clipsInheritScale);
    assert(state.projectNavigation.stepPasteMode == project::ProjectStepPasteMode::PAGE);

    StepSequencerScaleSettings expectedScale{
        .root = 2,
        .type = StepSequencerScaleType::WholeTone,
        .mode = StepSequencerScaleConstraintMode::ConstrainDown,
    };
    assert(sameScale(state.sequencerTracks.projectScaleSettings(), expectedScale));

    assert(state.sharedTrackEnabledMask.get() == 0x0003);
    assert(state.sharedTrackActive.get() == 1);
    assert(std::strcmp(state.pages.activePageData().name, "Snapshot") == 0);
    assert(state.pages.activePageData().cc[0] == 74);
    assert(state.pages.activePageData().values[0] == 0.75f);
    assert(state.pages.activeConfigs[0].cc == 74);
    assert(state.macros.slots[0].value.get() == 0.75f);

    const auto* restoredAutomation =
        core::state::macro::macroAutomationFindSlot(
            state.pages.automation,
            core::state::macro::MacroAutomationSlotAddress{
                .track = state.pages.currentActiveTrack(),
                .page = state.pages.currentActivePage(),
                .macro = 0,
            }
        );
    assert(restoredAutomation != nullptr);
    assert(core::state::macro::macroCurveStored(restoredAutomation->automation));
    assert(restoredAutomation->automation.playbackState ==
           core::state::macro::MacroCurvePlaybackState::OFF);
    assert(core::state::macro::macroCurveStored(restoredAutomation->modulation));
    assert(restoredAutomation->modulation.playbackState ==
           core::state::macro::MacroCurvePlaybackState::ACTIVE);
    assert(restoredAutomation->modulation.modulationOrigin ==
           core::state::macro::MacroModulationOrigin::CONVERTED_MEAN);
    assert(restoredAutomation->modulationDepth > 0.3699f &&
           restoredAutomation->modulationDepth < 0.3701f);

    assert(state.sequencer.pattern.length.get() == 15);
    assert(state.sequencer.pattern.stepsPerBeat.get() == 6);
    assert(state.sequencer.pattern.midiChannel.get() == 9);
    assert(state.sequencer.pattern.isEnabled(0));
    assert(state.sequencer.pattern.note[0] == 66);
    assert(state.sequencer.pattern.velocity[0] == 111);
    assert(state.sequencer.pattern.gate[0] == 88);

    std::cout << "[PASS] test_snapshot_capture_apply_restores_project_session\n";
}

void test_snapshot_project_routing_wins_on_apply() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));
    snapshot.project.routing.outputMidiChannels[1] = 12;

    state.resetMusicalProject();
    assert(project::applyProjectSnapshot(state, snapshot));

    assert(state.sequencerTracks.activeTrackIndex() == 1);
    assert(state.sequencerTracks.track(1).midiChannel.get() == 12);
    assert(state.sequencer.pattern.midiChannel.get() == 12);

    std::cout << "[PASS] test_snapshot_project_routing_wins_on_apply\n";
}

void test_incremental_capture_completes_the_same_snapshot_contract() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    project::ProjectSnapshotCapture capture;
    assert(capture.begin(state, snapshot));

    for (uint8_t phase = 0; phase < 3; ++phase) {
        const auto progress = capture.advance();
        assert(progress.status == project::ProjectSnapshotCapture::Status::IN_PROGRESS);
        assert(progress.modifiedCounter == 42);
        assert(capture.active());
    }

    const auto complete = capture.advance();
    assert(complete.status == project::ProjectSnapshotCapture::Status::COMPLETE);
    assert(complete.modifiedCounter == 42);
    assert(!capture.active());
    assert(snapshot.project.metadata.modifiedCounter == 42);
    assert(snapshot.macroTracks[1].pages[0].cc[0] == 74);
    assert(snapshot.sequencer.flat.tracks[1].note[0] == 66);

    std::cout << "[PASS] test_incremental_capture_completes_the_same_snapshot_contract\n";
}

void test_incremental_capture_rejects_a_mixed_revision() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProjectSession(state);

    project::ProjectSnapshot snapshot;
    project::ProjectSnapshotCapture capture;
    assert(capture.begin(state, snapshot));
    assert(capture.advance().status ==
           project::ProjectSnapshotCapture::Status::IN_PROGRESS);

    state.sequencer.setStepDataAt(0, 77, 111, 88);
    state.markProjectMutated();

    const auto stale = capture.advance();
    assert(stale.status == project::ProjectSnapshotCapture::Status::STALE);
    assert(stale.modifiedCounter == 42);
    assert(!capture.active());

    std::cout << "[PASS] test_incremental_capture_rejects_a_mixed_revision\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectSnapshot tests\n";
    std::cout << "==============================================\n\n";

    test_project_state_defaults_are_stable();
    test_snapshot_macro_tracks_keep_track_specific_defaults();
    test_snapshot_capture_apply_restores_project_session();
    test_snapshot_project_routing_wins_on_apply();
    test_incremental_capture_completes_the_same_snapshot_contract();
    test_incremental_capture_rejects_a_mixed_revision();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

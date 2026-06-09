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
        storages.macroWorkspace,
        storages.macroLibrary,
        storages.sequencerWorkspace,
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
    assert(std::strcmp(state.metadata.name.data(), "Untitled") == 0);
    assert(!state.metadata.dirty);
    assert(!state.metadata.hasSavedIdentity);
    assert(state.transport.tempoBpm == 120.0f);
    assert(state.transport.swingPercent == 0);
    assert(state.transport.runMode == 0);
    assert(sameScale(state.musical.scale, sequencer::defaultProjectScaleSettings()));
    assert(state.musical.patternsInheritScale);
    assert(state.musical.clipsInheritScale);

    for (uint8_t i = 0; i < state.routing.outputMidiChannels.size(); ++i) {
        assert(state.routing.outputMidiChannels[i] == i);
    }

    std::cout << "[PASS] test_project_state_defaults_are_stable\n";
}

void configureProjectSession(core::state::CoreState& state) {
    std::strncpy(
        state.project.metadata.id.data(),
        "P123",
        state.project.metadata.id.size() - 1
    );
    std::strncpy(
        state.project.metadata.name.data(),
        "Snapshot Test",
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

    StepSequencerScaleSettings scale{
        .root = 2,
        .type = StepSequencerScaleType::WholeTone,
        .mode = StepSequencerScaleConstraintMode::ConstrainDown,
    };
    assert(state.sequencerTracks.setProjectScaleSettings(scale));

    assert(state.setSharedTrackState(0x0003, 1));
    auto& page = state.pages.activePageData();
    std::strncpy(page.name, "Snapshot", sizeof(page.name) - 1);
    page.cc[0] = 74;
    page.values[0] = 0.75f;
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

    state.resetMusicalProject();
    assert(state.statusBar.tempo.get() == 120.0f);
    assert(state.sequencer.pattern.length.get() == sequencer::SequencerPatternState::DEFAULT_LENGTH);
    assert(state.sharedTrackActive.get() == 0);

    assert(project::applyProjectSnapshot(state, snapshot));

    assert(std::strcmp(state.project.metadata.id.data(), "P123") == 0);
    assert(std::strcmp(state.project.metadata.name.data(), "Snapshot Test") == 0);
    assert(state.project.metadata.modifiedCounter == 42);
    assert(state.project.metadata.dirty);
    assert(state.project.metadata.hasSavedIdentity);

    assert(state.statusBar.tempo.get() == 137.0f);
    assert(state.statusBar.tempoDisplay.get() == 137.0f);
    assert(state.projectNavigation.transportSwingPercent == 23);
    assert(state.projectNavigation.transportRunMode == 2);
    assert(!state.projectNavigation.patternsInheritScale);
    assert(state.projectNavigation.clipsInheritScale);

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

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectSnapshot tests\n";
    std::cout << "==============================================\n\n";

    test_project_state_defaults_are_stable();
    test_snapshot_capture_apply_restores_project_session();
    test_snapshot_project_routing_wins_on_apply();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

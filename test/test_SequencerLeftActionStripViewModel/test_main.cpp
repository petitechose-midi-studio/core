#include <cassert>
#include <iostream>

#include "../../src/state/CoreState.hpp"
#include "../../src/ui/font/StandaloneIcons.hpp"
#include "../../src/ui/sequencer/SequencerLeftActionStripViewModelBuilder.hpp"
#include "../../src/ui/sequencer/SequencerViewModelBuilder.hpp"
#include "../support/CoreStorages.hpp"

// Firmware-only projection sources are compiled directly against the native
// UI stubs, matching the existing view-model contracts.
#include "../../src/ui/sequencer/StepPropertyVisuals.cpp"
#include "../../src/ui/sequencer/SequencerQuickControlVisuals.cpp"
#include "../../src/ui/sequencer/SequencerLeftActionStripViewModelBuilder.cpp"

namespace {

using core::ui::ContextActionStripVisualState;
using test_support::CoreStorages;

core::ui::sequencer::SequencerViewModelSource sourceFor(
    core::state::CoreState& state,
    bool patternWorkspace = true
) {
    if (patternWorkspace && state.sequencer.clipWorkspace.matrixVisible()) {
        state.sequencer.clipWorkspace.enterPattern(0U, 0U);
    }
    return {
        .sequencer = state.sequencer,
        .clips = state.sequencerClips,
        .clipLaunches = state.sequencerClipLaunches,
        .tracks = state.sequencerTracks,
        .projectTracks = state.projectTracks,
        .trackNavigation = state.trackNavigation,
        .navigationFocus = state.structureNavigationFocus,
        .sharedTrackActive = state.sharedTrackActive,
        .sharedTrackEnabledMask = state.sharedTrackEnabledMask,
        .structureClipboard = state.structureClipboard,
        .statusBar = state.statusBar,
        .projectNavigation = state.projectNavigation,
        .trackActivations = state.sequencerTrackActivations,
    };
}

void test_clip_launcher_left_strip_exposes_region_and_selection_actions() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    auto& launcher = state.sequencer.clipWorkspace;
    launcher.reset(0U);

    auto props = core::ui::sequencer::buildSequencerLeftActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[1].icon == standalone::icons::CLIP);
    assert(props.slots[1].visualState == ContextActionStripVisualState::ACTIVE);

    launcher.beginSelection(0U, 0U);
    props = core::ui::sequencer::buildSequencerLeftActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[0].icon == standalone::icons::ACTION_BACKWARD);
    assert(props.slots[0].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[1].icon == standalone::icons::ACTION_MOVE);

    launcher.beginPlacement(
        core::state::sequencer::ClipWorkspaceOperation::MOVE_DESTINATION,
        1U
    );
    props = core::ui::sequencer::buildSequencerLeftActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[0].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[1].visualState == ContextActionStripVisualState::HIDDEN);

    std::cout << "[PASS] Clip Launcher left strip exposes region and selection\n";
}

void test_clip_launcher_move_is_disabled_when_the_track_is_full() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    for (uint8_t slot = 1U;
         slot < core::state::sequencer::SequencerClipGridState::SLOT_COUNT;
         ++slot) {
        assert(state.createSequencerClip({0U, slot}));
    }
    state.sequencer.clipWorkspace.beginSelection(0U, 1U);

    const auto props = core::ui::sequencer::buildSequencerLeftActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[1].icon == standalone::icons::ACTION_MOVE);
    assert(props.slots[1].visualState == ContextActionStripVisualState::DISABLED);
}

void expectCancel(const core::ui::ContextActionStripProps& props) {
    assert(props.visible);
    assert(props.slots[0].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[0].icon == standalone::icons::ACTION_CANCEL);
}

void test_selector_strip_projection_contract() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::STEP);

    state.sequencer.patternQuickControls.selecting.set(true);
    state.sequencer.stepContentSelector.selecting.set(true);
    auto props = core::ui::sequencer::buildSequencerLeftActionStripProps(
        sourceFor(state)
    );
    expectCancel(props);
    assert(props.slots[1].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[1].icon == core::ui::sequencer::visual::quickControlIconGlyph(
        state.sequencer.patternQuickControls.focusedItem.get()
    ));
    assert(props.slots[2].visualState == ContextActionStripVisualState::HIDDEN);

    state.sequencer.patternQuickControls.selecting.set(false);
    state.sequencer.stepContentSelector.selecting.set(false);
    state.sequencer.stepPropertyInlineSelector.selecting.set(true);
    props = core::ui::sequencer::buildSequencerLeftActionStripProps(sourceFor(state));
    expectCancel(props);
    assert(props.slots[1].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[1].icon == core::ui::sequencer::visual::propertyIconGlyph(
        state.sequencer.activeStepProperty.get()
    ));
    assert(props.slots[2].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[2].icon == standalone::icons::NOTE_PROP_RANDOM);

    state.sequencer.stepPropertyInlineSelector.selecting.set(false);
    state.sequencer.stepContentSelector.selecting.set(true);
    props = core::ui::sequencer::buildSequencerLeftActionStripProps(sourceFor(state));
    expectCancel(props);
    assert(props.slots[1].visualState == ContextActionStripVisualState::HIDDEN);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[2].icon == standalone::icons::NOTE_PROP_RANDOM);

    std::cout << "[PASS] Left selector strip projection contract\n";
}

void test_drum_pattern_lane_and_step_actions_are_distinct() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    assert(state.sequencerTracks.setTrackKind(
        0U,
        core::state::sequencer::SequencerTrackKind::DRUM,
        true,
        core::state::sequencer::DrumKitPreset::GENERAL_MIDI
    ));
    auto& drum = state.sequencer.drumSequencer;
    drum.bindTrack(0U, state.sequencerTracks.drumTrack(0U), state.sequencerTracks);
    drum.enterGrid();

    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    auto props = core::ui::sequencer::buildSequencerLeftActionStripProps(
        sourceFor(state)
    );
    assert(props.slots[1].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[1].icon == standalone::icons::LENGTH);
    assert(props.slots[2].visualState == ContextActionStripVisualState::HIDDEN);

    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::LANE);
    props = core::ui::sequencer::buildSequencerLeftActionStripProps(sourceFor(state));
    assert(props.slots[1].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[1].icon == standalone::icons::LENGTH);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ACTIVE);

    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::STEP);
    props = core::ui::sequencer::buildSequencerLeftActionStripProps(sourceFor(state));
    assert(props.slots[1].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[2].visualState == ContextActionStripVisualState::HIDDEN);

    std::cout << "[PASS] Drum Pattern/Lane/Step action strips are distinct\n";
}

}  // namespace

int main() {
    test_selector_strip_projection_contract();
    test_drum_pattern_lane_and_step_actions_are_distinct();
    test_clip_launcher_left_strip_exposes_region_and_selection_actions();
    test_clip_launcher_move_is_disabled_when_the_track_is_full();
    std::cout << "\nAll Sequencer left-action-strip tests passed.\n";
    return 0;
}

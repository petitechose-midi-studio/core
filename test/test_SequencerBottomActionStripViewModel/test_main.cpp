#include <cassert>
#include <cstring>
#include <iostream>

#include "../../src/state/CoreState.hpp"
#include "../../src/ui/font/StandaloneIcons.hpp"
#include "../../src/ui/sequencer/SequencerBottomActionStripViewModelBuilder.hpp"
#include "../../src/ui/sequencer/SequencerViewModelBuilder.hpp"
#include "../support/CoreStorages.hpp"

// Firmware-only projection sources are compiled directly against the native
// UI stubs, matching the existing view-model contracts.
#include "../../src/ui/sequencer/StepPropertyVisuals.cpp"
#include "../../src/ui/sequencer/SequencerTrackPasteProjection.cpp"
#include "../../src/ui/sequencer/SequencerBottomActionStripViewModelBuilder.cpp"

namespace {

using core::ui::ContextActionStripTone;
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

void test_clip_launcher_selection_strip_reuses_structure_grammar() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    auto& launcher = state.sequencer.clipWorkspace;
    launcher.reset(0U);

    launcher.beginSelection(0U, 0U);
    auto props = core::ui::sequencer::buildSequencerBottomActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[0].icon == standalone::icons::ACTION_REMOVE);
    assert(props.slots[0].visualState == ContextActionStripVisualState::ACTIVE);
    assert(std::strcmp(props.slots[1].labelText.data(), "1 selected") == 0);
    assert(props.slots[2].icon == standalone::icons::ACTION_COPY);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ACTIVE);

    state.statusBar.playing.set(true);
    props = core::ui::sequencer::buildSequencerBottomActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[0].visualState == ContextActionStripVisualState::DISABLED);
    state.statusBar.playing.set(false);

    launcher.beginPlacement(
        core::state::sequencer::
            ClipWorkspaceOperation::DUPLICATE_DESTINATION,
        0U,
        1U
    );
    props = core::ui::sequencer::buildSequencerBottomActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[0].visualState == ContextActionStripVisualState::DISABLED);
    assert(props.slots[2].icon == standalone::icons::ACTION_PLACE_TARGET);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ACTIVE);

    std::cout << "[PASS] Clip Launcher strip reuses selection grammar\n";
}

void test_clip_workspace_browse_strip_exposes_viewport_navigation() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    state.sequencer.clipWorkspace.reset(0U);
    state.sequencer.clipWorkspace.focus(0U, 0U);

    const auto props = core::ui::sequencer::buildSequencerBottomActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[0].icon == standalone::icons::ACTION_BACKWARD);
    assert(props.slots[0].visualState == ContextActionStripVisualState::ACTIVE);
    assert(!props.slots[0].iconRotated180);
    assert(props.slots[2].icon == standalone::icons::ACTION_BACKWARD);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[2].iconRotated180);
}

void test_clip_launcher_copy_uses_a_compatible_track_when_source_is_full() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);
    assert(state.setSharedTrackState(0x0003U, 0U));
    for (uint8_t slot = 1U;
         slot < core::state::sequencer::SequencerClipGridState::SLOT_COUNT;
         ++slot) {
        assert(state.createSequencerClip({0U, slot}));
    }
    state.sequencer.clipWorkspace.beginSelection(0U, 1U);

    auto props = core::ui::sequencer::buildSequencerBottomActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[2].icon == standalone::icons::ACTION_COPY);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ACTIVE);

    assert(state.sequencerTracks.setTrackKind(
        1U,
        core::state::sequencer::SequencerTrackKind::DRUM,
        true,
        core::state::sequencer::DrumKitPreset::GENERAL_MIDI
    ));
    props = core::ui::sequencer::buildSequencerBottomActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[2].visualState == ContextActionStripVisualState::DISABLED);
}

void expectPlacementStrip(
    const core::ui::ContextActionStripProps& props,
    const char* selectionLabel,
    ContextActionStripVisualState pasteVisual
) {
    assert(props.visible);
    assert(props.slots[0].visualState == ContextActionStripVisualState::HIDDEN);
    assert(props.slots[1].visualState == ContextActionStripVisualState::ACTIVE);
    assert(std::strcmp(props.slots[1].labelText.data(), selectionLabel) == 0);
    assert(props.slots[2].visualState == pasteVisual);
    assert(props.slots[2].tone == ContextActionStripTone::WARNING);
    assert(props.slots[2].icon == standalone::icons::ACTION_PASTE);
    assert(!props.slots[2].showLabel);
}

void test_selection_strip_projection_contract() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);

    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::PAGE);
    auto& pageSelection = state.sequencer.structureUi.pageSelection;
    pageSelection.active.set(true);
    pageSelection.placing.set(true);
    pageSelection.selectedMask.set(0x0001U);
    pageSelection.destinationMask.set(0x0001U);
    pageSelection.overwriteMask.set(0x0001U);

    auto props = core::ui::sequencer::buildSequencerBottomActionStripProps(
        sourceFor(state)
    );
    expectPlacementStrip(props, "1 selected", ContextActionStripVisualState::ACTIVE);
    assert(!props.slots[2].holdActive);

    state.sequencer.structureUi.pageHold.begin(
        core::state::StructureHoldAction::PASTE,
        42U
    );
    props = core::ui::sequencer::buildSequencerBottomActionStripProps(sourceFor(state));
    expectPlacementStrip(props, "1 selected", ContextActionStripVisualState::ARMED);
    assert(props.slots[2].holdActive);
    assert(props.slots[2].holdStartedAtMs == 42U);

    pageSelection.reset();
    state.sequencer.structureUi.pageHold.clear();

    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::STEP);
    auto& stepSelection = state.sequencer.structureUi.stepSelection;
    stepSelection.active.set(true);
    stepSelection.placing.set(true);
    stepSelection.setSelected(0U, true);
    props = core::ui::sequencer::buildSequencerBottomActionStripProps(sourceFor(state));
    assert(props.slots[0].visualState == ContextActionStripVisualState::HIDDEN);
    assert(std::strcmp(props.slots[1].labelText.data(), "1 selected") == 0);
    assert(props.slots[2].visualState == ContextActionStripVisualState::DISABLED);
    assert(props.slots[2].tone == ContextActionStripTone::DESTRUCTIVE);
    assert(props.slots[2].icon == standalone::icons::ACTION_PASTE);
    assert(!props.slots[2].showLabel);
    stepSelection.reset();

    assert(state.sequencerTracks.setTrackKind(
        0U,
        core::state::sequencer::SequencerTrackKind::DRUM,
        true,
        core::state::sequencer::DrumKitPreset::GENERAL_MIDI
    ));
    auto& drumTrack = state.sequencerTracks.drumTrack(0U);
    assert(state.structureClipboard.storeSequencerDrumLaneSelection(
        drumTrack,
        0x0003U,
        nullptr
    ));

    auto& drum = state.sequencer.drumSequencer;
    drum.phase = core::state::sequencer::DrumSequencerPhase::GRID;
    drum.drumTrack = &drumTrack;
    drum.laneSelection.active = true;
    drum.laneSelection.placing = true;
    drum.laneSelection.selectedMask = 0x0003U;
    drum.laneSelection.destinationMask = 0x000CU;
    drum.laneSelection.overwriteMask = 0x0004U;
    drum.laneSelection.clipboardRevision = state.structureClipboard.revision.get();

    props = core::ui::sequencer::buildSequencerBottomActionStripProps(sourceFor(state));
    expectPlacementStrip(props, "2 selected", ContextActionStripVisualState::ACTIVE);
    assert(!props.slots[2].holdActive);

    drum.laneSelection.reset();
    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::LANE);
    props = core::ui::sequencer::buildSequencerBottomActionStripProps(sourceFor(state));
    assert(props.slots[0].visualState == ContextActionStripVisualState::HIDDEN);
    assert(props.slots[1].visualState == ContextActionStripVisualState::HIDDEN);
    assert(props.slots[2].visualState == ContextActionStripVisualState::HIDDEN);

    assert(state.setSharedTrackState(0x0003U, 0U));
    state.sequencer.clipWorkspace.reset(0U);
    state.sequencer.clipWorkspace.focusTrackHeader(0U);
    props = core::ui::sequencer::buildSequencerBottomActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[0].visualState == ContextActionStripVisualState::HIDDEN);
    assert(props.slots[1].visualState == ContextActionStripVisualState::HIDDEN);
    assert(props.slots[2].visualState == ContextActionStripVisualState::HIDDEN);

    state.trackNavigation.hold.begin(
        core::state::StructureHoldAction::REMOVE,
        42U
    );
    props = core::ui::sequencer::buildSequencerBottomActionStripProps(
        sourceFor(state, false)
    );
    assert(props.slots[0].visualState == ContextActionStripVisualState::HIDDEN);
    assert(props.slots[1].visualState == ContextActionStripVisualState::HIDDEN);
    assert(props.slots[2].visualState == ContextActionStripVisualState::HIDDEN);

    std::cout << "[PASS] Selection strip projection contract\n";
}

}  // namespace

int main() {
    test_selection_strip_projection_contract();
    test_clip_workspace_browse_strip_exposes_viewport_navigation();
    test_clip_launcher_selection_strip_reuses_structure_grammar();
    test_clip_launcher_copy_uses_a_compatible_track_when_source_is_full();
    std::cout << "\nAll Sequencer bottom-action-strip tests passed.\n";
    return 0;
}

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
    core::state::CoreState& state
) {
    return {
        .sequencer = state.sequencer,
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

}  // namespace

int main() {
    test_selector_strip_projection_contract();
    std::cout << "\nAll Sequencer left-action-strip tests passed.\n";
    return 0;
}

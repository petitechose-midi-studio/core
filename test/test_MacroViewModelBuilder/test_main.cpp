#include <cassert>
#include <iostream>

#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroAutomationState.hpp"
#include "../../src/ui/font/StandaloneIcons.hpp"
#include "../../src/ui/view/MacroViewModelBuilder.hpp"
#include "../support/CoreStorages.hpp"

// MacroViewModelBuilder is not part of the native core library because the
// firmware UI runtime owns LVGL. This test compiles the pure projection builder
// directly against the native UI stubs under test/.
#include "../../src/ui/view/MacroViewModelBuilder.cpp"

namespace {

using core::ui::ContextActionStripTone;
using core::ui::ContextActionStripVisualState;
using test_support::CoreStorages;

core::ui::MacroViewModelSource sourceFor(core::state::CoreState& state) {
    return {
        .macros = state.macros,
        .pages = state.pages,
        .macroUi = state.macroUi,
        .trackNavigation = state.trackNavigation,
        .navigationFocus = state.structureNavigationFocus,
        .sharedTrackActive = state.sharedTrackActive,
        .sharedTrackEnabledMask = state.sharedTrackEnabledMask,
        .structureClipboard = state.structureClipboard,
        .statusBar = state.statusBar,
    };
}

core::state::macro::MacroAutomationSlotState& configureAutomation(
    core::state::CoreState& state,
    uint8_t macro,
    float value
) {
    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(
        state.pages.automation,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = macro,
        }
    );
    assert(slot != nullptr);

    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = 1.0f;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, value));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, value));
    assert(core::state::macro::macroAutomationAssignAutomation(
        state.pages.automation,
        *slot,
        lane
    ));
    return *slot;
}

void test_macro_slot_focus_shows_local_automation_actions() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings,
        storage.macroLibrary,
        storage.sequencerPatternLibrary,
        storage.sequencerSetLibrary
    );

    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::STEP);
    state.macroUi.focusedMacroSlot.set(0);

    const auto props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.visible);
    assert(props.slots[0].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[0].tone == ContextActionStripTone::DESTRUCTIVE);
    assert(props.slots[0].icon == standalone::icons::ACTION_CLEAR);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[2].tone == ContextActionStripTone::NEUTRAL);
    assert(props.slots[2].icon == standalone::icons::ACTION_COPY);

    std::cout << "[PASS] test_macro_slot_focus_shows_local_automation_actions\n";
}

void test_macro_slot_focus_shows_paste_when_automation_clipboard_is_available() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings,
        storage.macroLibrary,
        storage.sequencerPatternLibrary,
        storage.sequencerSetLibrary
    );

    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::STEP);
    state.macroUi.focusedMacroSlot.set(0);
    auto& slot = configureAutomation(state, 0, 0.42f);
    state.structureClipboard.storeMacroAutomation(state.pages.automation, slot);

    const auto props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.visible);
    assert(props.slots[0].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ARMED);
    assert(props.slots[2].tone == ContextActionStripTone::CONSTRUCTIVE);
    assert(props.slots[2].icon == standalone::icons::ACTION_PASTE);

    std::cout << "[PASS] test_macro_slot_focus_shows_paste_when_automation_clipboard_is_available\n";
}

void test_macro_add_slot_focus_dims_structure_actions() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings,
        storage.macroLibrary,
        storage.sequencerPatternLibrary,
        storage.sequencerSetLibrary
    );

    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::STEP);
    state.macroUi.focusedMacroSlot.set(1);

    const auto props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.visible);
    assert(props.slots[0].visualState == ContextActionStripVisualState::DIM);
    assert(props.slots[2].visualState == ContextActionStripVisualState::DIM);
    assert(props.slots[2].icon == standalone::icons::ACTION_COPY);

    std::cout << "[PASS] test_macro_add_slot_focus_dims_structure_actions\n";
}

}  // namespace

int main() {
    test_macro_slot_focus_shows_local_automation_actions();
    test_macro_slot_focus_shows_paste_when_automation_clipboard_is_available();
    test_macro_add_slot_focus_dims_structure_actions();
    std::cout << "\nAll MacroViewModelBuilder tests passed.\n";
    return 0;
}

#include <cassert>
#include <cmath>
#include <iostream>

#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroAutomationState.hpp"
#include "../../src/ui/font/StandaloneIcons.hpp"
#include "../../src/ui/view/MacroViewModelBuilder.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/ProjectControlTestUtils.hpp"

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

core::state::macro::MacroAutomationSlotAddress configureAutomation(
    core::state::CoreState& state,
    uint8_t macro,
    float value
) {
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = macro,
    };

    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = 1.0f;
    assert(core::state::macro::macroAutomationAppendPoint(lane, 0.0f, value));
    assert(core::state::macro::macroAutomationAppendPoint(lane, 1.0f, value));
    assert(test_support::project_control::assignAutomation(
        state.pages.control,
        address,
        lane
    ));
    return address;
}

void test_macro_slot_focus_shows_guarded_slot_actions() {
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
    assert(props.slots[0].icon == standalone::icons::ACTION_REMOVE);
    assert(!props.slots[0].holdActive);
    assert(props.slots[0].holdDurationMs ==
           Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[2].tone == ContextActionStripTone::NEUTRAL);
    assert(props.slots[2].icon == standalone::icons::ACTION_COPY);

    std::cout << "[PASS] test_macro_slot_focus_shows_guarded_slot_actions\n";
}

void test_macro_slot_focus_only_arms_paste_for_typed_slot_clipboard() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings,
        storage.macroLibrary,
        storage.sequencerPatternLibrary,
        storage.sequencerSetLibrary
    );

    state.structureNavigationFocus.set(core::state::StructureNavigationFocus::STEP);
    state.macroUi.focusedMacroSlot.set(0);
    const auto address = configureAutomation(state, 0, 0.42f);
    assert(state.structureClipboard.storeMacroAutomation(
        state.pages.control,
        address
    ));

    auto props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.visible);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[2].tone == ContextActionStripTone::NEUTRAL);
    assert(props.slots[2].icon == standalone::icons::ACTION_COPY);

    assert(state.structureClipboard.storeMacroSlot(
        state.pages,
        core::state::macro::MacroAutomationSlotAddress{
            .track = state.pages.currentActiveTrack(),
            .page = state.pages.currentActivePage(),
            .macro = 0,
        }
    ));
    props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.visible);
    assert(props.slots[0].visualState == ContextActionStripVisualState::ACTIVE);
    assert(props.slots[2].visualState == ContextActionStripVisualState::ARMED);
    assert(props.slots[2].tone == ContextActionStripTone::CONSTRUCTIVE);
    assert(props.slots[2].icon == standalone::icons::ACTION_PASTE);

    std::cout << "[PASS] test_macro_slot_focus_only_arms_paste_for_typed_slot_clipboard\n";
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

void test_macro_selection_delete_strip_projects_guard_lifecycle() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings,
        storage.macroLibrary,
        storage.sequencerPatternLibrary,
        storage.sequencerSetLibrary
    );
    state.pages.setPageEnabled(1, true);
    state.macroUi.pageSelection.active.set(true);
    state.macroUi.pageSelection.scope.set(
        core::state::StructureSelectionScope::PAGE
    );
    state.macroUi.pageSelection.cursorIndex.set(1);
    state.macroUi.pageSelection.selectedMask.set(0x0002);

    auto props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.slots[0].visualState ==
           ContextActionStripVisualState::AVAILABLE);
    assert(props.slots[0].tone == ContextActionStripTone::DESTRUCTIVE);
    assert(props.slots[0].icon == standalone::icons::ACTION_REMOVE);
    assert(!props.slots[0].holdActive);

    core::state::contextual::GuardedActionState guard;
    assert(core::state::contextual::beginGuardedActionPress(guard, 100, 1000));
    state.macroUi.selectionDeleteGuard.set(guard);
    props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.slots[0].visualState ==
           ContextActionStripVisualState::PRESSED);
    assert(!props.slots[0].holdActive);

    assert(core::state::contextual::armGuardedAction(guard, 100));
    assert(core::state::contextual::updateGuardedAction(guard, 300));
    state.macroUi.selectionDeleteGuard.set(guard);
    props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.slots[0].visualState ==
           ContextActionStripVisualState::ARMED);
    assert(props.slots[0].holdActive);
    assert(props.slots[0].holdStartedAtMs == 100);
    assert(props.slots[0].holdDurationMs == 1000);

    assert(core::state::contextual::cancelGuardedAction(guard));
    state.macroUi.selectionDeleteGuard.set(guard);
    props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.slots[0].visualState ==
           ContextActionStripVisualState::CANCELLED);
    assert(props.slots[0].tone == ContextActionStripTone::WARNING);
    assert(props.slots[0].icon == standalone::icons::ACTION_CANCEL);

    std::cout << "[PASS] test_macro_selection_delete_strip_projects_guard_lifecycle\n";
}

void test_macro_selection_delete_strip_projects_disabled_and_applied() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings,
        storage.macroLibrary,
        storage.sequencerPatternLibrary,
        storage.sequencerSetLibrary
    );
    state.pages.setPageEnabled(1, true);
    state.macroUi.pageSelection.active.set(true);
    state.macroUi.pageSelection.selectedMask.set(0);

    auto props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.slots[0].visualState ==
           ContextActionStripVisualState::DISABLED);

    state.macroUi.pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    core::state::contextual::OperationFeedbackState feedback;
    core::state::contextual::setOperationFeedback(
        feedback,
        core::state::contextual::ContextActionId::REMOVE,
        {},
        {},
        core::state::contextual::OperationFeedbackStatus::APPLIED,
        core::state::contextual::ContextActionReason::NONE,
        core::state::contextual::OperationFeedbackExpiryPolicy::AFTER_DURATION,
        1000,
        1200
    );
    state.macroUi.selectionDeleteFeedback.set(feedback);
    props = core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(props.slots[0].visualState ==
           ContextActionStripVisualState::APPLIED);
    assert(props.slots[0].tone == ContextActionStripTone::POSITIVE);
    assert(props.slots[0].icon == standalone::icons::ACTION_VALIDATE);
    assert(props.slots[2].visualState == ContextActionStripVisualState::HIDDEN);

    std::cout << "[PASS] test_macro_selection_delete_strip_projects_disabled_and_applied\n";
}

void test_macro_grid_distinguishes_stored_playback_modulation_and_manual() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings,
        storage.macroLibrary,
        storage.sequencerPatternLibrary,
        storage.sequencerSetLibrary
    );
    state.pages.setMacroSlotActive(0, true);
    const auto address = configureAutomation(state, 0, 0.42f);
    core::state::macro::MacroModulationShape shape;
    shape.durationBeats = 2.0f;
    assert(core::state::macro::macroModulationAppendPoint(
        shape,
        0.0f,
        -0.5f
    ));
    assert(core::state::macro::macroModulationAppendPoint(
        shape,
        1.0f,
        0.5f
    ));
    assert(test_support::project_control::assignModulation(
        state.pages.control,
        address,
        shape,
        0.75f
    ));

    auto props = core::ui::buildMacroViewFrameState(sourceFor(state)).macros[0];
    assert(props.automationStored && props.automationActive);
    assert(props.modulationStored && props.modulationActive);
    assert(!props.modulationPaused);

    assert(core::state::modulation::setProjectControlAutomationEnabled(
        state.pages.control,
        address,
        false
    ));
    props = core::ui::buildMacroViewFrameState(sourceFor(state)).macros[0];
    assert(props.automationStored && !props.automationActive);
    assert(props.modulationStored && props.modulationActive);

    state.macroUi.automationManualOverrideMask.set(0x0001);
    props = core::ui::buildMacroViewFrameState(sourceFor(state)).macros[0];
    assert(props.automationStored && !props.automationActive);
    assert(props.modulationStored && props.modulationActive);

    core::state::macro::MacroResolvedValue projection{};
    projection.base = 0.4f;
    projection.modulation = 0.2f;
    projection.resolved = 0.6f;
    projection.modulationActive = true;
    state.macroUi.setRuntimeProjection(0, projection, 0.75f);
    props = core::ui::buildMacroViewFrameState(sourceFor(state)).macros[0];
    assert(std::fabs(props.baseValue - 0.4f) < 0.0001f);
    assert(std::fabs(props.modulationDelta - 0.2f) < 0.0001f);
    assert(std::fabs(props.value - 0.6f) < 0.0001f);
    assert(std::fabs(props.modulationDepth - 0.75f) < 0.0001f);

    state.macroUi.automationManualOverrideMask.set(0);
    assert(core::state::modulation::setProjectControlModulationEnabled(
        state.pages.control,
        address,
        false
    ));
    props = core::ui::buildMacroViewFrameState(sourceFor(state)).macros[0];
    assert(props.modulationStored);
    assert(!props.modulationActive);

    std::cout
        << "[PASS] "
        << "test_macro_grid_distinguishes_stored_playback_modulation_and_manual\n";
}

void test_runtime_projection_revision_targets_one_macro_or_all() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings,
        storage.macroLibrary,
        storage.sequencerPatternLibrary,
        storage.sequencerSetLibrary
    );

    core::state::macro::MacroResolvedValue projection{};
    projection.base = 0.25f;
    projection.resolved = 0.25f;
    state.macroUi.setRuntimeProjection(3, projection, 0.0f);

    const uint32_t slotRevision =
        state.macroUi.runtimeProjectionRevision.get();
    assert(!core::state::macro::macroRuntimeProjectionRevisionTargetsAll(
        slotRevision
    ));
    assert(core::state::macro::macroRuntimeProjectionRevisionDirtyIndex(
        slotRevision
    ) == 3);

    state.macroUi.setRuntimeProjection(3, projection, 0.0f);
    assert(state.macroUi.runtimeProjectionRevision.get() == slotRevision);

    state.macroUi.clearRuntimeProjections();
    const uint32_t clearRevision =
        state.macroUi.runtimeProjectionRevision.get();
    assert(core::state::macro::macroRuntimeProjectionRevisionTargetsAll(
        clearRevision
    ));
    assert(core::state::macro::macroRuntimeProjectionRevisionDirtyIndex(
        clearRevision
    ) == -1);

    std::cout
        << "[PASS] "
        << "test_runtime_projection_revision_targets_one_macro_or_all\n";
}

}  // namespace

int main() {
    test_macro_slot_focus_shows_guarded_slot_actions();
    test_macro_slot_focus_only_arms_paste_for_typed_slot_clipboard();
    test_macro_add_slot_focus_dims_structure_actions();
    test_macro_selection_delete_strip_projects_guard_lifecycle();
    test_macro_selection_delete_strip_projects_disabled_and_applied();
    test_macro_grid_distinguishes_stored_playback_modulation_and_manual();
    test_runtime_projection_revision_targets_one_macro_or_all();
    std::cout << "\nAll MacroViewModelBuilder tests passed.\n";
    return 0;
}

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

#include "../../src/config/Timing.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroAutomationAddress.hpp"
#include "../../src/state/macro/MacroAutomationDomain.hpp"
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
        storage.settings
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
        storage.settings
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
        storage.settings
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



void test_macro_grid_distinguishes_stored_playback_modulation_and_manual() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings
    );
    state.pages.setMacroSlotActive(0, true);
    const auto address = configureAutomation(state, 0, 0.42f);
    test_support::project_control::ModulationShape shape;
    shape.durationBeats = 2.0f;
    assert(test_support::project_control::appendModulationPoint(
        shape,
        0.0f,
        -0.5f
    ));
    assert(test_support::project_control::appendModulationPoint(
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
    assert(props.modulationSourceCount == 1U);
    assert(!props.clippedLow && !props.clippedHigh);

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
    state.macroUi.setRuntimeProjection(0, 0, 0, projection, 0.75f);
    props = core::ui::buildMacroViewFrameState(sourceFor(state)).macros[0];
    assert(std::fabs(props.baseValue - 0.4f) < 0.0001f);
    assert(std::fabs(props.modulationDelta - 0.2f) < 0.0001f);
    assert(std::fabs(props.value - 0.6f) < 0.0001f);
    assert(std::fabs(props.modulationDepth - 0.75f) < 0.0001f);
    assert(!props.clippedLow && !props.clippedHigh);

    projection.base = 0.9f;
    projection.modulation = 0.2f;
    projection.resolved = 1.0f;
    state.macroUi.setRuntimeProjection(0, 0, 0, projection, 0.75f);
    props = core::ui::buildMacroViewFrameState(sourceFor(state)).macros[0];
    assert(!props.clippedLow && props.clippedHigh);

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
        storage.settings
    );

    core::state::macro::MacroResolvedValue projection{};
    projection.base = 0.25f;
    projection.resolved = 0.25f;
    state.macroUi.setRuntimeProjection(0, 0, 3, projection, 0.0f);

    const uint32_t contextRevision =
        state.macroUi.runtimeProjectionRevision.get();
    assert(core::state::macro::macroRuntimeProjectionRevisionTargetsAll(
        contextRevision
    ));

    projection.base = 0.5f;
    projection.resolved = 0.5f;
    state.macroUi.setRuntimeProjection(0, 0, 3, projection, 0.0f);
    const uint32_t slotRevision =
        state.macroUi.runtimeProjectionRevision.get();
    assert(!core::state::macro::macroRuntimeProjectionRevisionTargetsAll(
        slotRevision
    ));
    assert(core::state::macro::macroRuntimeProjectionRevisionDirtyIndex(
        slotRevision
    ) == 3);

    state.macroUi.setRuntimeProjection(0, 0, 3, projection, 0.0f);
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

void test_macro_performance_projection_explains_edit_and_shared_take() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings
    );
    state.pages.setMacroSlotActive(0, true);
    state.pages.setMacroSlotActive(2, true);

    auto strip = core::ui::buildMacroLeftActionStripProps(sourceFor(state));
    assert(strip.slots[0].visualState == ContextActionStripVisualState::HIDDEN);
    assert(strip.slots[1].visualState == ContextActionStripVisualState::ACTIVE);
    assert(strip.slots[1].icon == standalone::icons::MACRO_AUTOMATION);
    assert(strip.slots[2].visualState == ContextActionStripVisualState::ACTIVE);
    assert(strip.slots[2].icon == standalone::icons::KNOB);

    state.macroUi.performanceOverlayMode.set(
        core::state::macro::MacroPerformanceOverlayMode::EDIT
    );
    auto overlay = core::ui::buildMacroSlotPropertyOverlayProps(sourceFor(state));
    assert(overlay.visible);
    assert(std::strcmp(overlay.label, "EDIT") == 0);
    assert(std::strcmp(overlay.valueText.data(), "PRESS A MACRO") == 0);

    std::array<uint8_t, 8> bases{};
    bases[0] = 32U;
    bases[2] = 64U;
    state.macroUi.automationTake.arm(
        core::state::macro::MacroAutomationTakeTiming::BARS_4,
        0x0005U,
        bases
    );
    state.macroUi.automationTake.track = state.pages.currentActiveTrack();
    state.macroUi.automationTake.page = state.pages.currentActivePage();
    state.macroUi.performanceOverlayMode.set(
        core::state::macro::MacroPerformanceOverlayMode::AUTOMATION_TAKE
    );
    overlay = core::ui::buildMacroSlotPropertyOverlayProps(sourceFor(state));
    assert(!overlay.visible);
    auto header = core::ui::buildMacroHeaderBarProps(sourceFor(state));
    assert(header.automationTakePhase ==
           core::state::macro::MacroAutomationTakePhase::ARMED);
    assert(header.automationTakeTiming ==
           core::state::macro::MacroAutomationTakeTiming::BARS_4);

    assert(state.macroUi.automationTake.begin(100U, 200U, 123456U, 3U, 7U));
    state.macroUi.setRuntimeProjection(
        state.pages.currentActiveTrack(),
        state.pages.currentActivePage(),
        0U,
        {
            .base = 0.1f,
            .modulation = 0.05f,
            .resolved = 0.15f,
            .modulationStored = true,
            .modulationActive = true,
        },
        1.0f
    );
    assert(state.macroUi.automationTake.touch(0U, 48U, 1U));
    assert(state.macroUi.automationTake.touch(2U, 80U, 2U));
    overlay = core::ui::buildMacroSlotPropertyOverlayProps(sourceFor(state));
    assert(!overlay.visible);
    const auto frame = core::ui::buildMacroViewFrameState(sourceFor(state));
    assert(frame.macros[0].automationRecording);
    assert(!frame.macros[1].automationRecording);
    assert(frame.macros[2].automationRecording);
    assert(std::fabs(frame.macros[0].baseValue - (48.0f / 127.0f)) < 0.0001f);
    assert(std::fabs(frame.macros[0].modulationDelta - 0.05f) < 0.0001f);
    assert(std::fabs(
        frame.macros[0].value - (48.0f / 127.0f + 0.05f)
    ) < 0.0001f);

    std::cout
        << "[PASS] "
        << "test_macro_performance_projection_explains_edit_and_shared_take\n";
}

void test_macro_selection_projection_exposes_copy_collision_and_blocked_states() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings
    );
    auto selected = state.macroUi.slotSelection.selectedMask.get();
    selected.setBit(0U, true);
    selected.setBit(3U, true);
    state.macroUi.slotSelection.selectedMask.set(selected);
    state.macroUi.slotSelection.active.set(true);
    state.macroUi.slotSelection.cursorLinear.set(0U);

    auto strip =
        core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(strip.slots[0].visualState ==
           ContextActionStripVisualState::HIDDEN);
    assert(strip.slots[2].showLabel);
    assert(std::strcmp(
        strip.slots[2].labelText.data(),
        "CPY \xC2\xB7 2"
    ) == 0);
    auto sourceSlot =
        core::ui::buildMacroWidgetProps(sourceFor(state), 0U);
    assert(sourceSlot.selected);
    assert(sourceSlot.focused);

    state.macroUi.slotSelection.placing.set(true);
    state.macroUi.slotSelection.cursorLinear.set(10U);
    std::array<uint8_t, core::state::macro::PAGE_COUNT>
        destinations{};
    std::array<uint8_t, core::state::macro::PAGE_COUNT>
        overwrites{};
    destinations[1] =
        static_cast<uint8_t>((1U << 2U) | (1U << 5U));
    overwrites[1] = static_cast<uint8_t>(1U << 5U);
    state.macroUi.slotSelection.publishPlacement(
        destinations,
        overwrites,
        false,
        1U,
        2U,
        7U
    );

    strip =
        core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(strip.slots[2].tone == ContextActionStripTone::WARNING);
    assert(std::strcmp(
        strip.slots[2].labelText.data(),
        "PST \xC2\xB7 1 OVR"
    ) == 0);
    const auto freeTarget =
        core::ui::buildMacroWidgetProps(sourceFor(state), 2U);
    const auto overwriteTarget =
        core::ui::buildMacroWidgetProps(sourceFor(state), 5U);
    assert(freeTarget.placementPreview ==
           core::ui::MacroSlotPlacementPreview::FREE);
    assert(std::fabs(freeTarget.value - 0.5f) < 0.0001f);
    assert(overwriteTarget.placementPreview ==
           core::ui::MacroSlotPlacementPreview::OVERWRITE);
    const auto header =
        core::ui::buildMacroHeaderBarProps(sourceFor(state));
    assert(header.slotSelectionActive);
    assert(header.previewPage == 1U);
    assert(header.previewPageAddSlot);

    state.macroUi.slotSelection.publishPlacement(
        destinations,
        overwrites,
        true,
        1U,
        3U,
        7U
    );
    strip =
        core::ui::buildMacroBottomActionStripProps(sourceFor(state));
    assert(strip.slots[2].visualState ==
           ContextActionStripVisualState::DISABLED);
    assert(strip.slots[2].tone ==
           ContextActionStripTone::DESTRUCTIVE);
    assert(core::ui::buildMacroWidgetProps(
        sourceFor(state),
        2U
    ).placementPreview ==
           core::ui::MacroSlotPlacementPreview::BLOCKED);

    std::cout
        << "[PASS] Macro selection UI exposes Copy, overwrite and blocked states\n";
}

void test_macro_selection_uses_existing_preview_page_value() {
    CoreStorages storage;
    core::state::CoreState state(
        storage.settings
    );
    state.pages.setPageEnabled(1U, true);
    state.macros.slots[2U].value.set(0.15f);
    state.pages.tracks[0U].pages[1U].values[2U] = 0.75f;
    state.macroUi.slotSelection.active.set(true);
    state.macroUi.slotSelection.cursorLinear.set(10U);

    const auto preview =
        core::ui::buildMacroWidgetProps(sourceFor(state), 2U);
    assert(std::fabs(preview.value - 0.75f) < 0.0001f);

    std::cout
        << "[PASS] Macro selection uses the existing preview Page value\n";
}

}  // namespace

int main() {
    test_macro_slot_focus_shows_guarded_slot_actions();
    test_macro_slot_focus_only_arms_paste_for_typed_slot_clipboard();
    test_macro_add_slot_focus_dims_structure_actions();
    test_macro_grid_distinguishes_stored_playback_modulation_and_manual();
    test_runtime_projection_revision_targets_one_macro_or_all();
    test_macro_performance_projection_explains_edit_and_shared_take();
    test_macro_selection_projection_exposes_copy_collision_and_blocked_states();
    test_macro_selection_uses_existing_preview_page_value();
    std::cout << "\nAll MacroViewModelBuilder tests passed.\n";
    return 0;
}

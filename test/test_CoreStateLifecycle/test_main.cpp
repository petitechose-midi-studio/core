#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/time/Time.hpp>

#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../support/CoreStorages.hpp"

namespace {

uint32_t g_mock_now_ms = 0;

uint32_t mockTimeMs() {
    return g_mock_now_ms;
}

using test_support::CoreStorages;

void test_overlay_registration_supports_stacking_and_restore() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings);

    assert(state.overlays.current() == core::ui::OverlayType::NONE);
    assert(!state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());

    state.overlays.show(core::ui::OverlayType::MACRO_EDIT, false);
    assert(state.overlays.current() == core::ui::OverlayType::MACRO_EDIT);
    assert(state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());

    state.overlays.show(core::ui::OverlayType::MACRO_EDIT_SELECTOR, true);
    assert(state.overlays.current() == core::ui::OverlayType::MACRO_EDIT_SELECTOR);
    assert(state.macroEdit.visible.get());
    assert(state.macroEdit.selector.visible.get());

    state.overlays.hide();
    assert(state.overlays.current() == core::ui::OverlayType::MACRO_EDIT);
    assert(state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());

    state.overlays.hide();
    assert(state.overlays.current() == core::ui::OverlayType::NONE);
    assert(!state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());

    std::cout << "[PASS] test_overlay_registration_supports_stacking_and_restore\n";
}

void test_factory_reset_clears_transient_state_and_overlays() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings);

    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.macroEdit.openEditor(1, 2, 10, 1000);
    state.macroEdit.openValueSelector(0, 2);
    state.deviceSettings.openView();
    state.deviceSettings.openSelector(1, 1);
    state.sequencer.stepInlineFeedback.show(
        3,
        core::state::sequencer::StepProperty::VELOCITY,
        0
    );
    state.sequencer.patternQuickControls.selecting.set(true);
    const auto manualAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    assert(state.macroUi.manualOverrides.activate(manualAddress, 0.73f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);

    const uint32_t beforeRevision = state.configRevision.get();
    const uint32_t beforeRuntimeOwnerRevision = state.macroRuntimeOwnerRevision.get();
    state.factoryReset();

    assert(state.activeView.get() == core::ui::ViewType::MACRO);
    assert(state.overlays.current() == core::ui::OverlayType::NONE);
    assert(state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(state.deviceSettings.flowPhase.get() == core::state::DeviceSettingsFlowPhase::CLOSED);
    assert(!state.sequencer.stepInlineFeedback.visible.get());
    assert(!state.sequencer.patternQuickControls.selecting.get());
    assert(!state.macroUi.manualOverrides.activeFor(manualAddress));
    assert(state.macroRuntimeOwnerRevision.get() == beforeRuntimeOwnerRevision + 1U);
    assert(
        state.configRevision.get() ==
        core::state::macro::nextMacroConfigRevision(
            beforeRevision,
            core::state::macro::kMacroConfigDirtyAll
        )
    );

    std::cout << "[PASS] test_factory_reset_clears_transient_state_and_overlays\n";
}

void test_core_state_update_expires_inline_feedback() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings);

    g_mock_now_ms = 1000;
    state.sequencer.stepInlineFeedback.show(
        2,
        core::state::sequencer::StepProperty::PROBABILITY,
        g_mock_now_ms
    );
    assert(state.sequencer.stepInlineFeedback.visible.get());

    g_mock_now_ms += core::state::sequencer::SequencerStepInlineFeedbackState::DISPLAY_HOLD_MS - 1;
    state.update();
    assert(state.sequencer.stepInlineFeedback.visible.get());

    g_mock_now_ms += 1;
    state.update();
    assert(!state.sequencer.stepInlineFeedback.visible.get());

    std::cout << "[PASS] test_core_state_update_expires_inline_feedback\n";
}

void test_core_state_update_expires_status_bar_pulses() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings);

    g_mock_now_ms = 2000;
    state.statusBar.pulseNoteIn();
    state.statusBar.pulseSyncInput();
    state.statusBar.pulseBeat();

    assert(state.statusBar.noteInActive.get());
    assert(state.statusBar.syncInputPulse.get());
    assert(state.statusBar.beatPulse.get());

    g_mock_now_ms += 40;
    state.statusBar.pulseNoteIn();

    g_mock_now_ms = 2079;
    state.update();
    assert(state.statusBar.noteInActive.get());
    assert(state.statusBar.syncInputPulse.get());
    assert(state.statusBar.beatPulse.get());

    g_mock_now_ms = 2080;
    state.update();
    assert(state.statusBar.noteInActive.get());
    assert(!state.statusBar.syncInputPulse.get());
    assert(state.statusBar.beatPulse.get());

    g_mock_now_ms = 2119;
    state.update();
    assert(state.statusBar.noteInActive.get());
    assert(!state.statusBar.beatPulse.get());

    g_mock_now_ms = 2120;
    state.update();
    assert(!state.statusBar.noteInActive.get());
    assert(!state.statusBar.beatPulse.get());

    std::cout << "[PASS] test_core_state_update_expires_status_bar_pulses\n";
}

void test_reset_standalone_transient_ui_clears_context_owned_state() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings);

    state.macroEdit.openEditor(2, 1, 64, 1500);
    state.macroEdit.openModulatorPicker(4);
    state.deviceSettings.openView();
    state.deviceSettings.openSelector(2, 3);
    state.sequencer.stepEdit.visible.set(true);
    state.sequencer.stepPropertyInlineSelector.selecting.set(true);
    state.sequencer.patternQuickControls.selecting.set(true);
    state.setSharedTrackState(state.currentSharedTrackEnabledMask(), 3);
    const auto manualAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    assert(state.macroUi.manualOverrides.activate(manualAddress, 0.61f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    // A take temporarily removes Manual. A view/context teardown cancels the
    // gesture and must restore the prior Project-scoped runtime state.
    std::array<uint8_t,
               core::state::macro::MacroAutomationTakeState::VALUE_COLUMN_COUNT>
        takeBases{};
    state.macroUi.automationTake.arm(
        core::state::macro::MacroAutomationTakeTiming::HOLD,
        0x0001U,
        takeBases
    );
    state.macroUi.automationTake.track = manualAddress.track;
    state.macroUi.automationTake.page = manualAddress.page;
    assert(state.macroUi.automationTake.begin(1000U, 0U, 0U, 0U, 0U));
    state.macroUi.automationTake.manualRestoreMask = 0x0001U;
    state.macroUi.automationTake.previousManualValues[0] = 0.61f;
    assert(state.macroUi.manualOverrides.resume(manualAddress));
    core::state::macro::MacroWorkflow::setRuntimeValue(state.macros, 0, 0.93f);
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.activeView.set(core::ui::ViewType::MACRO);

    const uint32_t beforeRuntimeOwnerRevision = state.macroRuntimeOwnerRevision.get();
    state.resetStandaloneTransientUi();

    assert(!state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());
    assert(state.macroEdit.modulatorPickerIndex.get() == 0);
    assert(state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(!state.deviceSettings.visible.get());
    assert(state.deviceSettings.flowPhase.get() == core::state::DeviceSettingsFlowPhase::CLOSED);
    assert(!state.sequencer.stepEdit.visible.get());
    assert(!state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(!state.sequencer.patternQuickControls.selecting.get());
    float manualValue = 0.0f;
    assert(state.macroUi.manualOverrides.valueFor(manualAddress, manualValue));
    assert(manualValue > 0.60f && manualValue < 0.62f);
    assert(std::fabs(state.macros[0].value.get() - manualValue) < 0.0005f);
    assert((state.macroUi.automationManualOverrideMask.get() & 0x0001U) != 0U);
    assert(state.macroUi.automationTake.phase ==
           core::state::macro::MacroAutomationTakePhase::IDLE);
    assert(state.macroRuntimeOwnerRevision.get() == beforeRuntimeOwnerRevision);
    std::cout << "[PASS] test_reset_standalone_transient_ui_clears_context_owned_state\n";
}

void test_musical_project_reset_activates_one_new_macro_runtime_owner() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings);

    const uint32_t beforeRevision = state.macroRuntimeOwnerRevision.get();
    state.resetMusicalProject();
    assert(state.macroRuntimeOwnerRevision.get() == beforeRevision + 1U);

    std::cout << "[PASS] test_musical_project_reset_activates_one_new_macro_runtime_owner\n";
}

void test_macro_runtime_owner_revision_skips_zero_on_wrap() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings);

    state.macroRuntimeOwnerRevision.set(0xFFFF'FFFFU);
    state.requestMacroRuntimeOwnerActivation();
    assert(state.macroRuntimeOwnerRevision.get() == 1U);
    state.requestMacroRuntimeOwnerActivation();
    assert(state.macroRuntimeOwnerRevision.get() == 2U);

    std::cout << "[PASS] test_macro_runtime_owner_revision_skips_zero_on_wrap\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_overlay_registration_supports_stacking_and_restore();
    test_factory_reset_clears_transient_state_and_overlays();
    test_core_state_update_expires_inline_feedback();
    test_core_state_update_expires_status_bar_pulses();
    test_reset_standalone_transient_ui_clears_context_owned_state();
    test_musical_project_reset_activates_one_new_macro_runtime_owner();
    test_macro_runtime_owner_revision_skips_zero_on_wrap();
    std::cout << "\nAll CoreState lifecycle tests passed.\n";
    return 0;
}

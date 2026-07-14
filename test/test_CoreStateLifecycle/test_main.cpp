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

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

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

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.macroEdit.openEditor(1, 2, 10, 1000);
    state.macroEdit.openValueSelector(0, 2);
    state.deviceSettings.openView();
    state.deviceSettings.openSelector(1, 1);
    state.dataManager.openSession(core::state::DataManagerContext::SEQUENCER);
    state.dataManager.showDialog(core::state::DataManagerDialogMode::SET_LOAD_MODE, 1);
    state.dataManager.feedback.set("busy");
    state.sequencer.stepInlineFeedback.show(
        3,
        core::state::sequencer::StepProperty::VELOCITY,
        0
    );
    state.sequencer.patternQuickControls.selecting.set(true);
    state.overlays.show(core::ui::OverlayType::DATA_MANAGER, false);
    state.overlays.show(core::ui::OverlayType::DATA_MANAGER_DIALOG, true);
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
    assert(!state.dataManager.visible.get());
    assert(!state.dataManager.dialog.visible.get());
    assert(state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::CLOSED);
    assert(!state.sequencer.stepInlineFeedback.visible.get());
    assert(!state.sequencer.patternQuickControls.selecting.get());
    assert(std::strcmp(state.dataManager.feedback.get(), "") == 0);
    assert(!state.macroUi.manualOverrides.activeFor(manualAddress));
    assert(state.macroRuntimeOwnerRevision.get() == beforeRuntimeOwnerRevision + 1U);
    assert(std::strcmp(state.statusBar.pageName.get(), state.pages.activePageData().name) == 0);
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

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

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

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

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

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    state.macroEdit.openEditor(2, 1, 64, 1500);
    state.macroEdit.openTargetSelector(4);
    state.deviceSettings.openView();
    state.deviceSettings.openSelector(2, 3);
    state.dataManager.openSession(core::state::DataManagerContext::SEQUENCER);
    state.dataManager.showDialog(core::state::DataManagerDialogMode::COMMAND_PALETTE, 0);
    state.sequencer.stepEdit.visible.set(true);
    state.sequencer.stepPropertyInlineSelector.selecting.set(true);
    state.sequencer.patternQuickControls.selecting.set(true);
    state.trackNavigation.selection.active.set(true);
    state.trackNavigation.selection.scope.set(core::state::StructureSelectionScope::TRACK);
    state.trackNavigation.selection.cursorIndex.set(7);
    state.trackNavigation.selection.selectedMask.set(0x0080);
    state.setSharedTrackState(state.currentSharedTrackEnabledMask(), 3);
    const auto manualAddress = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = 0,
    };
    assert(state.macroUi.manualOverrides.activate(manualAddress, 0.61f) ==
           core::state::macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    // Recording temporarily removes Manual. A view/context teardown cancels
    // the gesture and must restore the prior Project-scoped runtime state.
    state.macroUi.automationRecording.active = true;
    state.macroUi.automationRecording.address = manualAddress;
    state.macroUi.automationRecording.restoreManualOnFailure = true;
    state.macroUi.automationRecording.previousManualValue = 0.61f;
    assert(state.macroUi.manualOverrides.resume(manualAddress));
    core::state::macro::MacroWorkflow::setRuntimeValue(state.macros, 0, 0.93f);
    state.activeView.set(core::ui::ViewType::SEQUENCER);
    state.activeView.set(core::ui::ViewType::MACRO);

    const uint32_t beforeRuntimeOwnerRevision = state.macroRuntimeOwnerRevision.get();
    state.resetStandaloneTransientUi();

    assert(!state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());
    assert(state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(!state.deviceSettings.visible.get());
    assert(state.deviceSettings.flowPhase.get() == core::state::DeviceSettingsFlowPhase::CLOSED);
    assert(!state.dataManager.visible.get());
    assert(state.dataManager.flowPhase.get() == core::state::DataManagerFlowPhase::CLOSED);
    assert(state.dataManager.context.get() == core::state::DataManagerContext::MACRO);
    assert(!state.sequencer.stepEdit.visible.get());
    assert(!state.sequencer.stepPropertyInlineSelector.selecting.get());
    assert(!state.sequencer.patternQuickControls.selecting.get());
    assert(!state.trackNavigation.selection.active.get());
    assert(state.trackNavigation.selection.cursorIndex.get() == 0);
    float manualValue = 0.0f;
    assert(state.macroUi.manualOverrides.valueFor(manualAddress, manualValue));
    assert(manualValue > 0.60f && manualValue < 0.62f);
    assert(std::fabs(state.macros[0].value.get() - manualValue) < 0.0005f);
    assert((state.macroUi.automationManualOverrideMask.get() & 0x0001U) != 0U);
    assert(!state.macroUi.automationRecording.active);
    assert(state.macroRuntimeOwnerRevision.get() == beforeRuntimeOwnerRevision);
    std::cout << "[PASS] test_reset_standalone_transient_ui_clears_context_owned_state\n";
}

void test_musical_project_reset_activates_one_new_macro_runtime_owner() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    const uint32_t beforeRevision = state.macroRuntimeOwnerRevision.get();
    state.resetMusicalProject();
    assert(state.macroRuntimeOwnerRevision.get() == beforeRevision + 1U);

    std::cout << "[PASS] test_musical_project_reset_activates_one_new_macro_runtime_owner\n";
}

void test_macro_runtime_owner_revision_skips_zero_on_wrap() {
    CoreStorages storage;
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

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

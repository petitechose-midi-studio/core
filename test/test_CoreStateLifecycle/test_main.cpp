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
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerStepContentDraftOps.hpp"
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
    const auto beforeSaveToken = state.projectSessionSaveToken();
    state.factoryReset();

    assert(state.activeView.get() == core::ui::ViewType::MACRO);
    assert(state.overlays.current() == core::ui::OverlayType::NONE);
    assert(state.macroEdit.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(state.deviceSettings.flowPhase.get() == core::state::DeviceSettingsFlowPhase::CLOSED);
    assert(!state.sequencer.stepInlineFeedback.visible.get());
    assert(!state.sequencer.patternQuickControls.selecting.get());
    assert(!state.macroUi.manualOverrides.activeFor(manualAddress));
    assert(state.macroRuntimeOwnerRevision.get() == beforeRuntimeOwnerRevision + 1U);
    const auto afterSaveToken = state.projectSessionSaveToken();
    assert(afterSaveToken.session != beforeSaveToken.session);
    assert(afterSaveToken.requestId == 1U);
    assert(state.hasPendingProjectSessionSave());
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
    state.projectNavigation.scaleConstrainEnabled = false;
    state.projectNavigation.patternsInheritScale = false;
    state.projectNavigation.clipsInheritScale = false;
    state.projectNavigation.stepPasteMode =
        core::state::project::ProjectStepPasteMode::WRAP;
    state.projectNavigation.ccLaneDefaultControllers = {2U, 12U, 75U, 72U};
    state.projectNavigation.transportSwingPercent = 31U;
    state.projectNavigation.transportRunMode = 2U;
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
    const auto beforeSaveToken = state.projectSessionSaveToken();
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
    assert(!state.projectNavigation.scaleConstrainEnabled);
    assert(!state.projectNavigation.patternsInheritScale);
    assert(!state.projectNavigation.clipsInheritScale);
    assert(state.projectNavigation.stepPasteMode ==
           core::state::project::ProjectStepPasteMode::WRAP);
    assert((state.projectNavigation.ccLaneDefaultControllers ==
            std::array<uint8_t, 4>{2U, 12U, 75U, 72U}));
    assert(state.projectNavigation.transportSwingPercent == 31U);
    assert(state.projectNavigation.transportRunMode == 2U);
    float manualValue = 0.0f;
    assert(state.macroUi.manualOverrides.valueFor(manualAddress, manualValue));
    assert(manualValue > 0.60f && manualValue < 0.62f);
    assert(std::fabs(state.macros[0].value.get() - manualValue) < 0.0005f);
    assert((state.macroUi.automationManualOverrideMask.get() & 0x0001U) != 0U);
    assert(state.macroUi.automationTake.phase ==
           core::state::macro::MacroAutomationTakePhase::IDLE);
    assert(state.macroRuntimeOwnerRevision.get() == beforeRuntimeOwnerRevision);
    assert(state.projectSessionSaveToken() == beforeSaveToken);
    std::cout << "[PASS] test_reset_standalone_transient_ui_clears_context_owned_state\n";
}

void test_standalone_teardown_abandons_unpublished_step_draft() {
    namespace seq = core::state::sequencer;

    CoreStorages storage;
    storage.initAll();
    core::state::CoreState state(storage.settings);

    const uint8_t publishedNote = state.sequencer.pattern.note[0];
    const auto beforeSaveToken = state.projectSessionSaveToken();
    assert(seq::beginStepContentDraft(
        state.sequencer,
        seq::SequencerStepContentDraftKind::CHORD,
        0U,
        seq::rootStepNodeId(0U)
    ));
    state.projectNavigation.currentNode.set(
        core::state::project::ProjectNodeId::MUSIC_SCALE
    );
    state.projectNavigation.focusedRow.set(3U);

    state.resetStandaloneTransientUi();

    assert(!state.sequencer.stepContentDraft.active.get());
    assert(state.sequencer.pattern.note[0] == publishedNote);
    assert(state.projectNavigation.currentNode.get() ==
           core::state::project::ProjectNodeId::OVERVIEW_ROOT);
    assert(state.projectNavigation.focusedRow.get() == 0U);
    assert(state.projectSessionSaveToken() == beforeSaveToken);
    std::cout << "[PASS] standalone teardown abandons unpublished Step draft\n";
}

void test_standalone_teardown_discards_only_invalid_pending_pattern_owner() {
    namespace seq = core::state::sequencer;

    CoreStorages storage;
    storage.initAll();
    core::state::CoreState state(storage.settings);

    assert(seq::sequencerHistoryOpenAccepted(
        state.beginOrContinueSequencerPatternHistoryCoalescing(
            0U,
            seq::StepProperty::NOTE,
            100U,
            seq::SequencerCoalescedPatternPayloadPlan::FlatOnly
        )
    ));
    assert(state.sequencer.setStepNoteAt(0U, 70U));
    assert(state.sealSequencerPatternHistoryCoalescing(true));
    assert(state.commitSequencerPatternHistoryCoalescingOutcome() ==
           seq::SequencerPatternHistoryCommitOutcome::Committed);
    const uint8_t committedUndoCount = state.sequencerHistory.undoCount();
    assert(committedUndoCount == 1U);

    assert(seq::sequencerHistoryOpenAccepted(
        state.beginOrContinueSequencerPatternHistoryCoalescing(
            0U,
            seq::StepProperty::NOTE,
            200U,
            seq::SequencerCoalescedPatternPayloadPlan::FlatOnly
        )
    ));
    assert(state.sequencer.setStepNoteAt(0U, 72U));
    const auto beforeSaveToken = state.projectSessionSaveToken();

    state.resetStandaloneTransientUi();

    assert(!state.hasPendingSequencerPatternHistoryCoalescing());
    assert(state.sequencer.pattern.note[0] == 72U);
    assert(state.sequencerHistory.undoCount() == committedUndoCount);
    assert(state.projectSessionSaveToken() == beforeSaveToken);
    std::cout << "[PASS] Standalone teardown discards only invalid Pattern owner\n";
}

void test_musical_project_reset_rejects_active_step_draft() {
    namespace seq = core::state::sequencer;

    CoreStorages storage;
    storage.initAll();
    core::state::CoreState state(storage.settings);

    state.statusBar.tempo.set(147.0f);
    const uint8_t publishedNote = state.sequencer.pattern.note[0];
    const auto beforeSaveToken = state.projectSessionSaveToken();
    assert(seq::beginStepContentDraft(
        state.sequencer,
        seq::SequencerStepContentDraftKind::CHORD,
        0U,
        seq::rootStepNodeId(0U)
    ));

    assert(state.resetMusicalProject() ==
           core::state::ProjectResetOutcome::DraftActive);
    assert(state.sequencer.stepContentDraft.active.get());
    assert(state.sequencer.stepContentDraft.blockedTransition ==
           seq::SequencerStepContentDraftBlockedTransition::RESET);
    assert(state.sequencer.pattern.note[0] == publishedNote);
    assert(state.statusBar.tempo.get() == 147.0f);
    assert(state.projectSessionSaveToken() == beforeSaveToken);
    std::cout << "[PASS] musical Project reset rejects active Step draft\n";
}

void test_musical_project_reset_reports_unavailable_pattern_history() {
    namespace seq = core::state::sequencer;

    CoreStorages storage;
    storage.initAll();
    core::state::CoreState state(storage.settings);

    state.statusBar.tempo.set(153.0f);
    assert(state.sequencer.setStepNoteAt(0U, 71U));
    const auto beforeSaveToken = state.projectSessionSaveToken();
    assert(seq::sequencerHistoryOpenAccepted(
        state.beginOrContinueSequencerPatternHistoryCoalescing(
            0U,
            seq::StepProperty::NOTE,
            100U,
            seq::SequencerCoalescedPatternPayloadPlan::FlatOnly
        )
    ));

    assert(state.resetMusicalProject() ==
           core::state::ProjectResetOutcome::HistoryUnavailable);
    assert(state.hasPendingSequencerPatternHistoryCoalescing());
    assert(state.sequencer.pattern.note[0] == 71U);
    assert(state.statusBar.tempo.get() == 153.0f);
    assert(state.projectSessionSaveToken() == beforeSaveToken);
    std::cout << "[PASS] musical Project reset reports unavailable Pattern history\n";
}

void test_flush_preserves_project_session_identity() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings);
    const auto beforeSaveToken = state.projectSessionSaveToken();

    state.flush();

    assert(state.projectSessionSaveToken() == beforeSaveToken);
    std::cout << "[PASS] test_flush_preserves_project_session_identity\n";
}

void test_musical_project_reset_activates_one_new_macro_runtime_owner() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings);

    const uint32_t beforeRevision = state.macroRuntimeOwnerRevision.get();
    const auto beforeSaveToken = state.projectSessionSaveToken();
    assert(state.resetMusicalProject() ==
           core::state::ProjectResetOutcome::Completed);
    assert(state.macroRuntimeOwnerRevision.get() == beforeRevision + 1U);
    const auto afterSaveToken = state.projectSessionSaveToken();
    assert(afterSaveToken.session != beforeSaveToken.session);
    assert(afterSaveToken.requestId == 1U);
    assert(state.hasPendingProjectSessionSave());

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
    test_standalone_teardown_abandons_unpublished_step_draft();
    test_standalone_teardown_discards_only_invalid_pending_pattern_owner();
    test_musical_project_reset_rejects_active_step_draft();
    test_musical_project_reset_reports_unavailable_pattern_history();
    test_flush_preserves_project_session_identity();
    test_musical_project_reset_activates_one_new_macro_runtime_owner();
    test_macro_runtime_owner_revision_skips_zero_on_wrap();
    std::cout << "\nAll CoreState lifecycle tests passed.\n";
    return 0;
}

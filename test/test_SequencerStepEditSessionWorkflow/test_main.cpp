#include <cassert>
#include <cstring>

#include <iostream>
#include <oc/api/ButtonAPI.hpp>
#include <oc/context/OverlayManager.hpp>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/input/InputBinding.hpp>

#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "handler/sequencer/SequencerStepEditSessionWorkflow.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "support/CoreStorages.hpp"
#include "support/InputTestHardware.hpp"

namespace {

namespace seq = core::state::sequencer;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;
namespace session_workflow = core::handler::sequencer::step_edit_session_workflow;

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() { return g_now_ms; }

struct SessionHarness {
    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    test_support::TestButtonHardware buttonHw;
    oc::api::ButtonAPI buttons;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::SequencerHistoryDomainServices history;
    core::handler::ButtonReleaseLatch<2> contextReleaseLatch;

    SessionHarness()
        : state(storages.settings), inputBinding(eventBus, mockTimeMs),
          buttons(inputBinding, buttonHw), overlays(state.overlays, buttons),
          history(core::handler::SequencerHistoryDomainServices::fromCoreState(state)) {
        overlays.setActiveViewProvider([]() { return 901; });
        overlays.registerCleanup(core::ui::OverlayType::SEQ_STEP_EDIT, 902);
        g_now_ms = 0;
    }
};

template <typename Mutation>
void applyRootStepEdit(SessionHarness& h, uint8_t step, Mutation mutation) {
    constexpr auto owner =
        core::state::sequencer::SequencerPreparedPatternEditOwner::StepEditSession;
    const auto descriptor = core::state::sequencer::SequencerHistoryDescriptor{
        .kind = core::state::sequencer::SequencerHistoryActionKind::StepEdit,
        .stepIndex = step,
    };
    assert(core::state::sequencer::sequencerHistoryOpenAccepted(h.history.beginPreparedPatternEdit(
               owner, step, core::state::sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly,
               descriptor)));
    const bool changed = mutation();
    assert(!core::state::sequencer::sequencerPreparedPatternEditSealFailed(
        h.history.sealPreparedPatternEdit(owner, step, changed, descriptor)
    ));
}

void test_open_session_resolves_page_step() {
    SessionHarness h;
    h.state.sequencer.pattern.setContentLength(16);
    h.state.sequencer.page.set(1);

    assert(session_workflow::openForMacroInPage(h.state.sequencer, h.history, h.overlays, 2));

    assert(h.state.sequencer.stepEdit.stepIndex.get() == 10);
    assert(h.state.sequencer.focusedStep.get() == 10);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == step_edit_rows::ACTIVATED);
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);
    assert(session_workflow::shouldCloseFromMacro(h.state.sequencer, 2));
    assert(!session_workflow::shouldCloseFromMacro(h.state.sequencer, 3));

    std::cout << "[PASS] test_open_session_resolves_page_step\n";
}

void test_close_commits_live_step_edit_history() {
    SessionHarness h;
    h.state.sequencer.pattern.setContentLength(8);
    h.state.sequencer.pattern.note[3] = 60;

    assert(session_workflow::openForMacroInPage(h.state.sequencer, h.history, h.overlays, 3));
    applyRootStepEdit(h, 3, [&]() { return h.state.sequencer.setStepNoteAt(3, 72); });

    assert(
        session_workflow::close(h.state.sequencer, h.history, h.contextReleaseLatch, h.overlays));

    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_close_commits_live_step_edit_history\n";
}

void test_back_to_parent_content_restores_parent_context_row() {
    SessionHarness h;
    h.state.sequencer.pattern.setContentLength(8);

    const auto rootNode = core::state::sequencer::rootStepNodeId(2);
    const auto micro =
        core::state::sequencer::createMicroSequence(h.state.sequencer.pattern, rootNode, 2);
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(h.state.sequencer, rootNode,
                                                                 micro.id));

    assert(session_workflow::openForMacroInPage(h.state.sequencer, h.history, h.overlays, 0));
    h.state.sequencer.stepEdit.focusedRow.set(step_edit_rows::PROPERTY_OFFSET);

    assert(session_workflow::backToParentContent(h.state.sequencer, h.history));

    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(h.state.sequencer.stepEdit.stepIndex.get() == h.state.sequencer.focusedStep.get());
    assert(h.state.sequencer.stepEdit.focusedRow.get() == step_edit_rows::MICRO_SEQUENCE);

    std::cout << "[PASS] test_back_to_parent_content_restores_parent_context_row\n";
}

void test_root_retarget_wraps_pages_and_separates_step_history() {
    SessionHarness h;
    h.state.sequencer.pattern.setContentLength(12);
    h.state.sequencer.page.set(0);
    oc::note::sequencer::StepBitMask128 enabled{};
    enabled.setBit(7, true);
    h.state.sequencer.pattern.enabledMask.set(enabled);

    assert(session_workflow::openForMacroInPage(h.state.sequencer, h.history, h.overlays, 7));
    applyRootStepEdit(h, 7, [&]() { return h.state.sequencer.setStepNoteAt(7, 72); });

    assert(session_workflow::retargetRootStep(h.state.sequencer, h.history, 1));
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 8);
    assert(h.state.sequencer.focusedStep.get() == 8);
    assert(h.state.sequencer.page.get() == 1);
    assert(!h.state.sequencer.pattern.enabledMask.get().test(8));
    assert(h.state.sequencerHistory.undoCount() == 1);

    applyRootStepEdit(h, 8, [&]() { return h.state.sequencer.setStepVelocityAt(8, 31); });
    assert(session_workflow::retargetRootStep(h.state.sequencer, h.history, -1));
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 7);
    assert(h.state.sequencer.page.get() == 0);
    assert(h.state.sequencerHistory.undoCount() == 2);

    assert(session_workflow::retargetRootStep(h.state.sequencer, h.history, -1));
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 6);

    // Reopen the first Step and prove reverse wrap uses the real length, not
    // the visible eight-Step page.
    h.state.sequencer.stepEdit.stepIndex.set(0);
    h.state.sequencer.focusedStep.set(0);
    h.state.sequencer.page.set(0);
    assert(session_workflow::retargetRootStep(h.state.sequencer, h.history, -1));
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 11);
    assert(h.state.sequencer.page.get() == 1);

    std::cout << "[PASS] test_root_retarget_wraps_pages_and_separates_step_history\n";
}

void test_failed_generic_commit_keeps_retarget_ui_exact_and_retryable() {
    SessionHarness h;
    h.state.sequencer.pattern.setContentLength(12);
    h.state.sequencer.page.set(0);
    oc::note::sequencer::StepBitMask128 enabled{};
    enabled.setBit(7, true);
    h.state.sequencer.pattern.enabledMask.set(enabled);

    assert(session_workflow::openForMacroInPage(h.state.sequencer, h.history, h.overlays, 7));

    const uint8_t stepBefore = h.state.sequencer.stepEdit.stepIndex.get();
    const uint8_t focusBefore = h.state.sequencer.focusedStep.get();
    const uint8_t pageBefore = h.state.sequencer.page.get();
    const uint8_t rowBefore = h.state.sequencer.stepEdit.focusedRow.get();
    const auto overlayBefore = h.overlays.current();
    const uint8_t undoBefore = h.state.sequencerHistory.undoCount();
    const uint32_t feedbackRevisionBefore = h.state.sequencer.historyFeedback.revision.get();

    // Leave a legacy StepProperty transaction between begin and seal. The
    // StepEdit owner-specific commit sees NoPending, then the generic typed
    // barrier must reject this incomplete owner before changing UI target.
    assert(
        seq::sequencerHistoryOpenAccepted(h.state.beginOrContinueSequencerPatternHistoryCoalescing(
        0U, seq::StepProperty::NOTE, 100U, seq::SequencerCoalescedPatternPayloadPlan::FlatOnly)));
    assert(!session_workflow::retargetRootStep(h.state.sequencer, h.history, 1));

    assert(h.state.hasPendingSequencerPatternHistoryCoalescing());
    assert(h.state.sequencer.stepEdit.stepIndex.get() == stepBefore);
    assert(h.state.sequencer.focusedStep.get() == focusBefore);
    assert(h.state.sequencer.page.get() == pageBefore);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == rowBefore);
    assert(h.overlays.current() == overlayBefore);
    assert(h.state.sequencerHistory.undoCount() == undoBefore);
    assert(h.state.sequencer.historyFeedback.revision.get() == feedbackRevisionBefore + 1U);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line1.data(), "NO CHANGE") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line2.data(), "History unavailable") == 0);
    assert(std::strcmp(h.state.sequencer.historyFeedback.line3.data(), "") == 0);

    // Recover the incomplete no-op, then prove the same transition succeeds.
    assert(h.state.sealSequencerPatternHistoryCoalescing(false));
    assert(session_workflow::retargetRootStep(h.state.sequencer, h.history, 1));
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 8U);
    assert(h.state.sequencer.focusedStep.get() == 8U);
    assert(h.state.sequencer.page.get() == 1U);

    std::cout << "[PASS] failed generic commit preserves StepEdit target and permits exact retry\n";
}

}  // namespace

int main() {
    test_open_session_resolves_page_step();
    test_close_commits_live_step_edit_history();
    test_back_to_parent_content_restores_parent_context_row();
    test_root_retarget_wraps_pages_and_separates_step_history();
    test_failed_generic_commit_keeps_retarget_ui_exact_and_retryable();

    std::cout << "\nAll SequencerStepEditSessionWorkflow tests passed.\n";
    return 0;
}

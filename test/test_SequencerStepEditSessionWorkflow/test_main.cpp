#include <cassert>
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

namespace step_edit_rows = core::state::sequencer::step_edit_rows;
namespace session_workflow = core::handler::sequencer::step_edit_session_workflow;

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

struct SessionHarness {
    test_support::CoreStorages storages;
    core::state::CoreState state;
    oc::core::event::EventBus eventBus;
    oc::core::input::InputBinding inputBinding;
    test_support::TestButtonHardware buttonHw;
    oc::api::ButtonAPI buttons;
    oc::context::OverlayManager<core::ui::OverlayType> overlays;
    core::handler::SequencerHistoryDomainServices history;
    core::handler::ButtonReleaseLatch<8> openReleaseLatch;
    core::handler::ButtonReleaseLatch<2> contextReleaseLatch;
    session_workflow::StepEditHistorySnapshot snapshot{};
    bool snapshotValid = false;

    SessionHarness()
        : state(storages.settings,
                storages.macroLibrary,
                storages.sequencerPatternLibrary,
                storages.sequencerSetLibrary)
        , inputBinding(eventBus, mockTimeMs)
        , buttons(inputBinding, buttonHw)
        , overlays(state.overlays, buttons)
        , history(core::handler::SequencerHistoryDomainServices::fromCoreState(state)) {
        overlays.setActiveViewProvider([]() { return 901; });
        overlays.registerCleanup(core::ui::OverlayType::SEQ_STEP_EDIT, 902);
        g_now_ms = 0;
    }
};

void test_open_session_resolves_page_step_and_latches_open_release() {
    SessionHarness h;
    h.state.sequencer.pattern.length.set(16);
    h.state.sequencer.page.set(1);

    assert(session_workflow::openForMacroInPage(
        h.state.sequencer,
        h.history,
        h.openReleaseLatch,
        h.overlays,
        h.snapshot,
        h.snapshotValid,
        2
    ));

    assert(h.snapshotValid);
    assert(h.state.sequencer.stepEdit.stepIndex.get() == 10);
    assert(h.state.sequencer.focusedStep.get() == 10);
    assert(h.state.sequencer.stepEdit.focusedRow.get() == step_edit_rows::ACTIVATED);
    assert(h.overlays.current() == core::ui::OverlayType::SEQ_STEP_EDIT);
    assert(!session_workflow::shouldCloseFromMacro(h.openReleaseLatch, h.state.sequencer, 2));
    assert(session_workflow::shouldCloseFromMacro(h.openReleaseLatch, h.state.sequencer, 2));
    assert(!session_workflow::shouldCloseFromMacro(h.openReleaseLatch, h.state.sequencer, 3));

    std::cout << "[PASS] test_open_session_resolves_page_step_and_latches_open_release\n";
}

void test_close_commits_live_step_edit_history() {
    SessionHarness h;
    h.state.sequencer.pattern.length.set(8);
    h.state.sequencer.pattern.note[3] = 60;

    assert(session_workflow::openForMacroInPage(
        h.state.sequencer,
        h.history,
        h.openReleaseLatch,
        h.overlays,
        h.snapshot,
        h.snapshotValid,
        3
    ));
    assert(h.state.sequencer.setStepNoteAt(3, 72));

    session_workflow::close(
        h.state.sequencer,
        h.history,
        h.openReleaseLatch,
        h.contextReleaseLatch,
        h.overlays,
        h.snapshot,
        h.snapshotValid
    );

    assert(!h.snapshotValid);
    assert(!h.state.sequencer.stepEdit.visible.get());
    assert(h.overlays.current() == core::ui::OverlayType::NONE);
    assert(h.state.sequencerHistory.undoCount() == 1);

    std::cout << "[PASS] test_close_commits_live_step_edit_history\n";
}

void test_back_to_parent_content_restores_parent_context_row() {
    SessionHarness h;
    h.state.sequencer.pattern.length.set(8);

    const auto rootNode = core::state::sequencer::rootStepNodeId(2);
    const auto micro = core::state::sequencer::createMicroSequence(
        h.state.sequencer.pattern,
        rootNode,
        2
    );
    assert(micro.ok);
    assert(core::state::sequencer::enterMicroSequenceContentView(
        h.state.sequencer,
        rootNode,
        micro.id
    ));

    assert(session_workflow::openForMacroInPage(
        h.state.sequencer,
        h.history,
        h.openReleaseLatch,
        h.overlays,
        h.snapshot,
        h.snapshotValid,
        0
    ));
    h.state.sequencer.stepEdit.focusedRow.set(step_edit_rows::PROPERTY_OFFSET);

    assert(session_workflow::backToParentContent(
        h.state.sequencer,
        h.history,
        h.snapshot,
        h.snapshotValid
    ));

    assert(core::state::sequencer::isRootContentView(h.state.sequencer));
    assert(h.state.sequencer.stepEdit.stepIndex.get() == h.state.sequencer.focusedStep.get());
    assert(h.state.sequencer.stepEdit.focusedRow.get() == step_edit_rows::MICRO_SEQUENCE);
    assert(h.snapshotValid);

    std::cout << "[PASS] test_back_to_parent_content_restores_parent_context_row\n";
}

}  // namespace

int main() {
    test_open_session_resolves_page_step_and_latches_open_release();
    test_close_commits_live_step_edit_history();
    test_back_to_parent_content_restores_parent_context_row();

    std::cout << "\nAll SequencerStepEditSessionWorkflow tests passed.\n";
    return 0;
}

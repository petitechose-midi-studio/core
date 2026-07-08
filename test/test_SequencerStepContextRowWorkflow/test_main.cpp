#include <cassert>
#include <iostream>

#include "handler/sequencer/SequencerStepContextRowWorkflow.hpp"
#include "state/CoreState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"
#include "support/CoreStorages.hpp"

namespace {

namespace step_context_row_workflow =
    core::handler::sequencer::step_context_row_workflow;
namespace step_edit_rows = core::state::sequencer::step_edit_rows;

core::state::CoreState makeState(test_support::CoreStorages& storage) {
    return core::state::CoreState(
        storage.settings,
        storage.macroLibrary,
        storage.sequencerPatternLibrary,
        storage.sequencerSetLibrary
    );
}

bool rootStepHasMicroSequence(
    const core::state::sequencer::SequencerPatternState& pattern,
    uint8_t step
) {
    return core::state::sequencer::stepNodeHasMicroSequence(
        pattern,
        core::state::sequencer::rootStepNodeId(step)
    );
}

bool rootStepHasCycleStates(
    const core::state::sequencer::SequencerPatternState& pattern,
    uint8_t step
) {
    return core::state::sequencer::stepNodeHasCycleStateSet(
        pattern,
        core::state::sequencer::rootStepNodeId(step)
    );
}

void test_focused_context_row_opens_or_creates_matching_child() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    sequencer.pattern.length.set(8);

    sequencer.stepEdit.focusedRow.set(step_edit_rows::MICRO_SEQUENCE);
    auto result = step_context_row_workflow::openOrCreateFocusedContextChild(
        sequencer,
        1
    );
    assert(result.opened);
    assert(result.created);
    assert(core::state::sequencer::isMicroSequenceContentView(sequencer));
    assert(core::state::sequencer::activeContentLength(sequencer) ==
           core::state::sequencer::DEFAULT_MICRO_SEQUENCE_LENGTH);

    assert(core::state::sequencer::leaveContentView(sequencer));
    sequencer.stepEdit.focusedRow.set(step_edit_rows::CYCLE_STATES);
    result = step_context_row_workflow::openOrCreateFocusedContextChild(sequencer, 2);
    assert(result.opened);
    assert(result.created);
    assert(core::state::sequencer::isCycleStatesContentView(sequencer));
    assert(core::state::sequencer::activeContentLength(sequencer) ==
           core::state::sequencer::DEFAULT_CYCLE_STATE_COUNT);

    std::cout << "[PASS] test_focused_context_row_opens_or_creates_matching_child\n";
}

void test_copy_paste_requires_focused_child_kind() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    auto& clipboard = state.structureClipboard;
    sequencer.pattern.length.set(8);

    sequencer.stepEdit.focusedRow.set(step_edit_rows::MICRO_SEQUENCE);
    auto result = step_context_row_workflow::openOrCreateFocusedContextChild(
        sequencer,
        0
    );
    assert(result.opened);
    assert(core::state::sequencer::leaveContentView(sequencer));

    assert(step_context_row_workflow::copyFocusedContextChildToClipboard(
        sequencer,
        0,
        clipboard
    ));

    sequencer.stepEdit.focusedRow.set(step_edit_rows::CYCLE_STATES);
    assert(!step_context_row_workflow::canPasteFocusedContextChild(
        sequencer,
        3,
        clipboard
    ));
    assert(!step_context_row_workflow::pasteFocusedContextChildFromClipboard(
        sequencer,
        3,
        clipboard
    ));
    assert(!rootStepHasCycleStates(sequencer.pattern, 3));

    sequencer.stepEdit.focusedRow.set(step_edit_rows::MICRO_SEQUENCE);
    assert(step_context_row_workflow::canPasteFocusedContextChild(
        sequencer,
        3,
        clipboard
    ));
    assert(step_context_row_workflow::pasteFocusedContextChildFromClipboard(
        sequencer,
        3,
        clipboard
    ));
    assert(rootStepHasMicroSequence(sequencer.pattern, 3));

    std::cout << "[PASS] test_copy_paste_requires_focused_child_kind\n";
}

void test_clear_focused_context_child() {
    test_support::CoreStorages storage;
    auto state = makeState(storage);
    auto& sequencer = state.sequencer;
    sequencer.pattern.length.set(8);
    sequencer.stepEdit.focusedRow.set(step_edit_rows::CYCLE_STATES);

    auto result = step_context_row_workflow::openOrCreateFocusedContextChild(
        sequencer,
        4
    );
    assert(result.opened);
    assert(core::state::sequencer::leaveContentView(sequencer));
    assert(rootStepHasCycleStates(sequencer.pattern, 4));

    assert(step_context_row_workflow::focusedContextHasChild(sequencer, 4));
    assert(step_context_row_workflow::clearFocusedContextChild(sequencer, 4));
    assert(!rootStepHasCycleStates(sequencer.pattern, 4));

    std::cout << "[PASS] test_clear_focused_context_child\n";
}

}  // namespace

int main() {
    test_focused_context_row_opens_or_creates_matching_child();
    test_copy_paste_requires_focused_child_kind();
    test_clear_focused_context_child();

    std::cout << "\nAll SequencerStepContextRowWorkflow tests passed.\n";
    return 0;
}

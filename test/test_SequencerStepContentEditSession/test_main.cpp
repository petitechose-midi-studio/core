#include <cassert>
#include <iostream>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerState.hpp"
#include "../../src/state/sequencer/SequencerStepContentEditSession.hpp"

namespace {

using core::state::sequencer::SequencerState;
using core::state::sequencer::SequencerStepContentEditSession;
using core::state::sequencer::StepContentChildKind;
using core::state::sequencer::StepContentContextKind;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::StepSequencerGraphLimits;

void test_open_root_context_does_not_allocate_graph() {
    SequencerState state;
    SequencerStepContentEditSession session;

    assert(session.openRootStepContext(3));
    assert(session.active());
    assert(core::state::sequencer::graphView(state.pattern) == nullptr);

    const auto view = session.current();
    assert(view.active);
    assert(view.kind == StepContentContextKind::ROOT_STEP);
    assert(view.rootStep == 3);
    assert(view.depth == 0);

    std::cout << "[PASS] test_open_root_context_does_not_allocate_graph\n";
}

void test_create_micro_sequence_enters_child_and_reuses_existing_content() {
    SequencerState state;
    SequencerStepContentEditSession session;
    assert(session.openRootStepContext(2));

    const auto first = session.createOrOpenMicroSequence(state.pattern, 3);
    assert(first.ok);
    assert(!first.limitReached);
    assert(!first.openedExisting);
    assert(core::state::sequencer::stepHasMicroSequence(state.pattern, 2));

    auto view = session.current();
    assert(view.kind == StepContentContextKind::MICRO_SEQUENCE);
    assert(view.rootStep == 2);
    assert(view.length == 3);
    assert(view.depth == 1);

    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    const uint16_t stepNodeCount = graph->stepNodeCount;

    assert(session.leaveChildContext());
    const auto second = session.createOrOpenMicroSequence(state.pattern, 6);
    assert(second.ok);
    assert(second.openedExisting);
    assert(core::state::sequencer::graphView(state.pattern)->stepNodeCount == stepNodeCount);
    assert(session.current().length == 3);

    std::cout << "[PASS] test_create_micro_sequence_enters_child_and_reuses_existing_content\n";
}

void test_cycle_states_and_micro_sequence_can_coexist() {
    SequencerState state;
    SequencerStepContentEditSession session;
    assert(session.openRootStepContext(4));

    assert(session.createOrOpenMicroSequence(state.pattern, 2).ok);
    assert(session.leaveChildContext());
    assert(session.createOrOpenCycleStates(state.pattern, 4).ok);

    assert(core::state::sequencer::stepHasMicroSequence(state.pattern, 4));
    assert(core::state::sequencer::stepHasCycleStates(state.pattern, 4));

    const auto rootNode = core::state::sequencer::rootStepNodeId(4);
    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    assert(graph->stepNodes[rootNode].has(STEP_NODE_CHILD_SEQUENCE));
    assert(graph->stepNodes[rootNode].has(STEP_NODE_CYCLE_SET));

    std::cout << "[PASS] test_cycle_states_and_micro_sequence_can_coexist\n";
}

void test_targeted_child_removal_keeps_sibling_content() {
    SequencerState state;
    SequencerStepContentEditSession session;
    assert(session.openRootStepContext(5));

    assert(session.createOrOpenMicroSequence(state.pattern, 2).ok);
    assert(session.leaveChildContext());
    assert(session.createOrOpenCycleStates(state.pattern, 3).ok);
    assert(session.leaveChildContext());

    assert(session.removeFocusedChild(state.pattern, StepContentChildKind::CYCLE_STATES));
    assert(core::state::sequencer::stepHasMicroSequence(state.pattern, 5));
    assert(!core::state::sequencer::stepHasCycleStates(state.pattern, 5));

    assert(session.removeFocusedChild(state.pattern, StepContentChildKind::MICRO_SEQUENCE));
    assert(!core::state::sequencer::stepHasMicroSequence(state.pattern, 5));

    std::cout << "[PASS] test_targeted_child_removal_keeps_sibling_content\n";
}

void test_remove_current_child_context_returns_to_parent() {
    SequencerState state;
    SequencerStepContentEditSession session;
    assert(session.openRootStepContext(1));
    assert(session.createOrOpenCycleStates(state.pattern, 2).ok);
    assert(session.current().kind == StepContentContextKind::CYCLE_STATES);

    assert(session.removeCurrentChildContext(state.pattern));
    const auto view = session.current();
    assert(view.kind == StepContentContextKind::ROOT_STEP);
    assert(view.rootStep == 1);
    assert(!core::state::sequencer::stepHasCycleStates(state.pattern, 1));

    std::cout << "[PASS] test_remove_current_child_context_returns_to_parent\n";
}

void test_max_depth_blocks_creation_before_graph_mutation() {
    SequencerState state;
    SequencerStepContentEditSession session;
    assert(session.openRootStepContext(0));

    for (uint8_t depth = 0; depth < StepSequencerGraphLimits::MAX_DEPTH; ++depth) {
        const auto result = session.createOrOpenMicroSequence(state.pattern, 1);
        assert(result.ok);
    }
    assert(session.maxDepthReached());

    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    const uint16_t stepNodeCount = graph->stepNodeCount;

    const auto blocked = session.createOrOpenMicroSequence(state.pattern, 1);
    assert(!blocked.ok);
    assert(blocked.limitReached);
    assert(core::state::sequencer::graphView(state.pattern)->stepNodeCount == stepNodeCount);

    std::cout << "[PASS] test_max_depth_blocks_creation_before_graph_mutation\n";
}

}  // namespace

int main() {
    test_open_root_context_does_not_allocate_graph();
    test_create_micro_sequence_enters_child_and_reuses_existing_content();
    test_cycle_states_and_micro_sequence_can_coexist();
    test_targeted_child_removal_keeps_sibling_content();
    test_remove_current_child_context_returns_to_parent();
    test_max_depth_blocks_creation_before_graph_mutation();

    std::cout << "\nAll SequencerStepContentEditSession tests passed.\n";
    return 0;
}

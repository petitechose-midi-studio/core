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
using core::state::sequencer::StepContentCreationBlockReason;
using core::state::sequencer::StepContentContextKind;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_GATE_OFFSET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::STEP_NODE_NUDGE_OFFSET;
using oc::note::sequencer::STEP_NODE_PROBABILITY_OFFSET;
using oc::note::sequencer::STEP_NODE_VELOCITY_OFFSET;
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

void test_default_child_lengths_match_product_contract() {
    SequencerState state;
    SequencerStepContentEditSession session;
    assert(session.openRootStepContext(6));

    assert(session.createOrOpenMicroSequence(state.pattern).ok);
    assert(session.current().kind == StepContentContextKind::MICRO_SEQUENCE);
    assert(session.current().length == SequencerStepContentEditSession::DEFAULT_MICRO_SEQUENCE_LENGTH);

    assert(session.leaveChildContext());
    assert(session.createOrOpenCycleStates(state.pattern).ok);
    assert(session.current().kind == StepContentContextKind::CYCLE_STATES);
    assert(session.current().length == SequencerStepContentEditSession::DEFAULT_CYCLE_STATE_COUNT);

    std::cout << "[PASS] test_default_child_lengths_match_product_contract\n";
}

void test_focused_child_queries_follow_current_context() {
    SequencerState state;
    SequencerStepContentEditSession session;
    assert(session.openRootStepContext(7));

    assert(!session.focusedStepHasMicroSequence(state.pattern));
    assert(!session.focusedStepHasCycleStates(state.pattern));

    assert(session.createOrOpenMicroSequence(state.pattern).ok);
    assert(session.leaveChildContext());
    assert(session.focusedStepHasMicroSequence(state.pattern));
    assert(!session.focusedStepHasCycleStates(state.pattern));

    assert(session.createOrOpenMicroSequence(state.pattern).ok);
    assert(session.focusLocalStep(1));
    assert(!session.focusedStepHasCycleStates(state.pattern));

    assert(session.createOrOpenCycleStates(state.pattern).ok);
    assert(session.leaveChildContext());
    assert(session.focusedStepHasCycleStates(state.pattern));

    std::cout << "[PASS] test_focused_child_queries_follow_current_context\n";
}

void test_child_creation_availability_reports_blocking_reason() {
    SequencerState state;
    SequencerStepContentEditSession session;

    auto availability = session.childCreationAvailability(
        state.pattern,
        StepContentChildKind::MICRO_SEQUENCE,
        SequencerStepContentEditSession::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    assert(!availability.canCreateOrOpen);
    assert(!availability.opensExisting);
    assert(availability.blockedReason == StepContentCreationBlockReason::INACTIVE_CONTEXT);

    assert(session.openRootStepContext(0));
    availability = session.childCreationAvailability(
        state.pattern,
        StepContentChildKind::MICRO_SEQUENCE,
        SequencerStepContentEditSession::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    assert(availability.canCreateOrOpen);
    assert(!availability.opensExisting);
    assert(availability.blockedReason == StepContentCreationBlockReason::NONE);

    assert(session.createOrOpenMicroSequence(state.pattern).ok);
    assert(session.leaveChildContext());
    availability = session.childCreationAvailability(
        state.pattern,
        StepContentChildKind::MICRO_SEQUENCE,
        SequencerStepContentEditSession::DEFAULT_MICRO_SEQUENCE_LENGTH
    );
    assert(availability.canCreateOrOpen);
    assert(availability.opensExisting);
    assert(availability.blockedReason == StepContentCreationBlockReason::NONE);

    availability = session.childCreationAvailability(
        state.pattern,
        StepContentChildKind::CYCLE_STATES,
        static_cast<uint8_t>(StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET + 1U)
    );
    assert(!availability.canCreateOrOpen);
    assert(!availability.opensExisting);
    assert(availability.blockedReason == StepContentCreationBlockReason::GRAPH_LIMIT_REACHED);

    std::cout << "[PASS] test_child_creation_availability_reports_blocking_reason\n";
}

void test_focused_property_edits_target_current_context_only() {
    SequencerState state;
    SequencerStepContentEditSession session;
    assert(session.openRootStepContext(8));
    assert(session.createOrOpenMicroSequence(state.pattern).ok);
    assert(session.focusLocalStep(1));

    assert(session.setFocusedNoteOffset(state.pattern, 5));
    assert(session.setFocusedVelocityOffset(state.pattern, -12));
    assert(session.setFocusedGateOffset(state.pattern, 24));
    assert(session.setFocusedNudgeOffset(state.pattern, -3));
    assert(session.setFocusedProbabilityOffset(state.pattern, -40));

    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    const auto* rootNode = graph->stepNode(core::state::sequencer::rootStepNodeId(8));
    assert(rootNode != nullptr);
    assert(rootNode->has(STEP_NODE_CHILD_SEQUENCE));
    assert(!rootNode->has(STEP_NODE_NOTE_OFFSET));
    assert(!rootNode->has(STEP_NODE_VELOCITY_OFFSET));
    assert(!rootNode->has(STEP_NODE_GATE_OFFSET));
    assert(!rootNode->has(STEP_NODE_NUDGE_OFFSET));
    assert(!rootNode->has(STEP_NODE_PROBABILITY_OFFSET));

    const auto* sequence = graph->sequence(rootNode->childSequenceId);
    assert(sequence != nullptr);
    const auto* childNode = graph->stepNode(static_cast<uint16_t>(sequence->firstStepNode + 1));
    assert(childNode != nullptr);
    assert(childNode->has(STEP_NODE_NOTE_OFFSET));
    assert(childNode->noteOffset == 5);
    assert(childNode->has(STEP_NODE_VELOCITY_OFFSET));
    assert(childNode->velocityOffset == -12);
    assert(childNode->has(STEP_NODE_GATE_OFFSET));
    assert(childNode->gateOffset == 24);
    assert(childNode->has(STEP_NODE_NUDGE_OFFSET));
    assert(childNode->nudgeOffset == -3);
    assert(childNode->has(STEP_NODE_PROBABILITY_OFFSET));
    assert(childNode->probabilityOffset == -40);

    assert(session.setFocusedNoteOffset(state.pattern, 0));
    assert(session.setFocusedVelocityOffset(state.pattern, 0));
    childNode = core::state::sequencer::graphView(state.pattern)->stepNode(
        static_cast<uint16_t>(sequence->firstStepNode + 1)
    );
    assert(childNode != nullptr);
    assert(!childNode->has(STEP_NODE_NOTE_OFFSET));
    assert(!childNode->has(STEP_NODE_VELOCITY_OFFSET));

    std::cout << "[PASS] test_focused_property_edits_target_current_context_only\n";
}

void test_micro_sequence_resize_uses_reserved_capacity() {
    SequencerState state;
    SequencerStepContentEditSession session;
    assert(session.openRootStepContext(9));
    assert(session.createOrOpenMicroSequence(state.pattern).ok);
    assert(session.current().length == SequencerStepContentEditSession::DEFAULT_MICRO_SEQUENCE_LENGTH);

    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    const auto* rootNode = graph->stepNode(core::state::sequencer::rootStepNodeId(9));
    assert(rootNode != nullptr);
    const auto* sequence = graph->sequence(rootNode->childSequenceId);
    assert(sequence != nullptr);
    const uint16_t firstNode = sequence->firstStepNode;
    const uint16_t reservedEnd =
        static_cast<uint16_t>(firstNode + StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);
    assert(graph->stepNodeCount >= reservedEnd);

    assert(session.resizeCurrentMicroSequence(
        state.pattern,
        StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP
    ));
    assert(session.current().length == StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);
    assert(session.focusLocalStep(
        static_cast<uint8_t>(StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP - 1U)
    ));
    assert(session.setFocusedNoteOffset(state.pattern, 11));

    graph = core::state::sequencer::graphView(state.pattern);
    sequence = graph->sequence(rootNode->childSequenceId);
    assert(sequence != nullptr);
    assert(sequence->length == StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);
    const auto* lastNode = graph->stepNode(
        static_cast<uint16_t>(
            sequence->firstStepNode + StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP - 1U
        )
    );
    assert(lastNode != nullptr);
    assert(lastNode->noteOffset == 11);

    assert(!session.resizeCurrentMicroSequence(
        state.pattern,
        static_cast<uint8_t>(StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP + 1U)
    ));

    std::cout << "[PASS] test_micro_sequence_resize_uses_reserved_capacity\n";
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
    test_default_child_lengths_match_product_contract();
    test_focused_child_queries_follow_current_context();
    test_child_creation_availability_reports_blocking_reason();
    test_focused_property_edits_target_current_context_only();
    test_micro_sequence_resize_uses_reserved_capacity();
    test_targeted_child_removal_keeps_sibling_content();
    test_remove_current_child_context_returns_to_parent();
    test_max_depth_blocks_creation_before_graph_mutation();

    std::cout << "\nAll SequencerStepContentEditSession tests passed.\n";
    return 0;
}

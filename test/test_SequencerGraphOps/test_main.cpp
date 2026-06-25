#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>

#include <oc/note/sequencer/StepSequencerExpander.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

#include "../../src/sequencer/SequencerRuntimeStateSync.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerSnapshotOps.hpp"

namespace {

using core::state::sequencer::SequencerPatternSnapshot;
using core::state::sequencer::SequencerState;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
using oc::note::sequencer::STEP_NODE_CHORD_MODE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::StepBitMask128;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSource;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerExpander;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerRuntimeState;

void enableStep(SequencerState& state, uint8_t step) {
    auto mask = state.pattern.enabledMask.get();
    mask.setBit(step, true);
    state.pattern.enabledMask.set(mask);
}

StepSequencerRuntimeState makeRuntime(const SequencerPatternSnapshot& snapshot) {
    StepSequencerRuntimeState runtime{};
    core::sequencer::syncRuntimeState(runtime, snapshot);
    return runtime;
}

void test_graph_root_is_allocated_once() {
    SequencerState state;

    assert(core::state::sequencer::ensureGraphRoot(state.pattern));
    const uint32_t revision = state.pattern.graphRevision.get();
    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    assert(graph->enabled);
    assert(graph->rootSequenceId == 0);
    assert(graph->sequenceCount == 1);
    assert(graph->stepNodeCount == SequencerState::MAX_STEPS);

    assert(core::state::sequencer::ensureGraphRoot(state.pattern));
    assert(state.pattern.graphRevision.get() == revision);

    std::cout << "[PASS] test_graph_root_is_allocated_once\n";
}

void test_pattern_without_graph_stays_unallocated_through_snapshot_copy() {
    SequencerState source;
    SequencerState target;

    assert(core::state::sequencer::graphView(source.pattern) == nullptr);

    SequencerPatternSnapshot snapshot;
    core::state::sequencer::captureSnapshot(source.pattern, snapshot);
    core::state::sequencer::applySnapshot(target.pattern, snapshot);

    assert(source.pattern.graph.get() == nullptr);
    assert(target.pattern.graph.get() == nullptr);
    assert(core::state::sequencer::graphView(target.pattern) == nullptr);

    core::state::sequencer::copyPatternState(target.pattern, source.pattern);
    assert(target.pattern.graph.get() == nullptr);

    std::cout << "[PASS] test_pattern_without_graph_stays_unallocated_through_snapshot_copy\n";
}

void test_micro_sequence_exports_to_open_control_graph() {
    SequencerState state;
    state.pattern.length.set(4);
    state.pattern.note[0] = 60;
    enableStep(state, 0);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern,
        rootNode,
        2
    );
    assert(sequence.ok);

    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    const auto* child = graph->sequence(sequence.id);
    assert(child != nullptr);
    assert(child->length == 2);
    assert(graph->stepNodes[rootNode].has(STEP_NODE_CHILD_SEQUENCE));

    const auto childNode = child->firstStepNode;
    assert(core::state::sequencer::setNodeNoteOffset(state.pattern, childNode, 2));
    assert(graph->stepNodes[childNode].has(STEP_NODE_NOTE_OFFSET));

    SequencerPatternSnapshot snapshot;
    core::state::sequencer::captureSnapshot(state.pattern, snapshot);
    auto runtime = makeRuntime(snapshot);

    const auto expansion = StepSequencerExpander::expandRootStep(
        runtime,
        *graph,
        0,
        0,
        24,
        0,
        true
    );

    assert(expansion.count == 2);
    assert(expansion.notes[0].localTick == 0);
    assert(expansion.notes[0].spanTicks == 12);
    assert(expansion.notes[0].variation.resolved.note == 62);
    assert(expansion.notes[1].localTick == 12);
    assert(expansion.notes[1].spanTicks == 12);
    assert(expansion.notes[1].variation.resolved.note == 60);

    std::cout << "[PASS] test_micro_sequence_exports_to_open_control_graph\n";
}

void test_step_node_child_presence_helpers_validate_targets() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);

    assert(!core::state::sequencer::stepNodeHasMicroSequence(state.pattern, rootNode));
    assert(!core::state::sequencer::stepNodeHasCycleStateSet(state.pattern, rootNode));

    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern,
        rootNode,
        2
    );
    assert(sequence.ok);
    assert(core::state::sequencer::stepNodeHasMicroSequence(state.pattern, rootNode));
    assert(!core::state::sequencer::stepNodeHasCycleStateSet(state.pattern, rootNode));

    const auto cycleSet = core::state::sequencer::createCycleStateSet(
        state.pattern,
        rootNode,
        3
    );
    assert(cycleSet.ok);
    assert(core::state::sequencer::stepNodeHasMicroSequence(state.pattern, rootNode));
    assert(core::state::sequencer::stepNodeHasCycleStateSet(state.pattern, rootNode));

    assert(core::state::sequencer::clearNodeChildSequence(state.pattern, rootNode));
    assert(!core::state::sequencer::stepNodeHasMicroSequence(state.pattern, rootNode));
    assert(core::state::sequencer::stepNodeHasCycleStateSet(state.pattern, rootNode));

    assert(core::state::sequencer::clearNodeCycleStateSet(state.pattern, rootNode));
    assert(!core::state::sequencer::stepNodeHasMicroSequence(state.pattern, rootNode));
    assert(!core::state::sequencer::stepNodeHasCycleStateSet(state.pattern, rootNode));

    std::cout << "[PASS] test_step_node_child_presence_helpers_validate_targets\n";
}

void test_pattern_copy_preserves_graph() {
    SequencerState source;
    const auto rootNode = core::state::sequencer::rootStepNodeId(3);
    const auto cycleSet = core::state::sequencer::createCycleStateSet(
        source.pattern,
        rootNode,
        3
    );
    assert(cycleSet.ok);
    const auto* sourceGraph = core::state::sequencer::graphView(source.pattern);
    assert(sourceGraph != nullptr);
    assert(core::state::sequencer::setNodeEnabledOverride(
        source.pattern,
        sourceGraph->cycleSets[cycleSet.id].firstStateNode + 1,
        false
    ));

    SequencerState target;
    core::state::sequencer::copyPatternState(target.pattern, source.pattern);

    assert(target.pattern.graphRevision.get() == source.pattern.graphRevision.get());
    const auto* targetGraph = core::state::sequencer::graphView(target.pattern);
    assert(targetGraph != nullptr);
    assert(targetGraph->enabled);
    assert(targetGraph->cycleSetCount == 1);
    assert(targetGraph->cycleSets[cycleSet.id].length == 3);
    assert(targetGraph->stepNodeCount == sourceGraph->stepNodeCount);

    std::cout << "[PASS] test_pattern_copy_preserves_graph\n";
}

void test_runtime_signature_tracks_graph_revision() {
    SequencerState state;
    SequencerPatternSnapshot before;
    core::state::sequencer::captureSnapshot(state.pattern, before);
    auto beforeSignature = core::sequencer::captureRuntimeStateSignature(before);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern,
        rootNode,
        2
    );
    assert(sequence.ok);

    SequencerPatternSnapshot after;
    core::state::sequencer::captureSnapshot(state.pattern, after);
    auto afterSignature = core::sequencer::captureRuntimeStateSignature(after);

    assert(!beforeSignature.matches(afterSignature));
    assert(afterSignature.graphRevision == state.pattern.graphRevision.get());

    std::cout << "[PASS] test_runtime_signature_tracks_graph_revision\n";
}

void test_create_micro_sequence_reuses_existing_child() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto first = core::state::sequencer::createMicroSequence(
        state.pattern,
        rootNode,
        2
    );
    assert(first.ok);
    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    const uint16_t stepNodeCount = graph->stepNodeCount;

    const auto second = core::state::sequencer::createMicroSequence(
        state.pattern,
        rootNode,
        4
    );
    assert(second.ok);
    assert(second.id == first.id);
    assert(core::state::sequencer::graphView(state.pattern)->stepNodeCount == stepNodeCount);

    std::cout << "[PASS] test_create_micro_sequence_reuses_existing_child\n";
}

void test_cycle_state_set_resizes_to_reserved_capacity() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto cycleSet = core::state::sequencer::createCycleStateSet(
        state.pattern,
        rootNode,
        4
    );
    assert(cycleSet.ok);

    auto* graph = state.pattern.graph.get();
    assert(graph != nullptr);
    const auto* set = graph->cycleSet(cycleSet.id);
    assert(set != nullptr);
    assert(set->length == 4);
    assert(graph->stepNodeCount == SequencerState::MAX_STEPS +
                                       StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET);

    const uint16_t firstState = set->firstStateNode;
    assert(core::state::sequencer::setNodeNoteOffset(state.pattern, firstState + 3U, 5));
    assert(core::state::sequencer::resizeCycleStateSet(state.pattern, cycleSet.id, 16));

    graph = state.pattern.graph.get();
    set = graph->cycleSet(cycleSet.id);
    assert(set != nullptr);
    assert(set->length == 16);
    assert(graph->stepNode(firstState + 3U)->noteOffset == 5);
    assert(graph->stepNode(firstState + 15U) != nullptr);

    assert(core::state::sequencer::resizeCycleStateSet(state.pattern, cycleSet.id, 1));
    assert(graph->cycleSet(cycleSet.id)->length == 1);
    assert(!core::state::sequencer::resizeCycleStateSet(state.pattern, cycleSet.id, 17));

    std::cout << "[PASS] test_cycle_state_set_resizes_to_reserved_capacity\n";
}

void test_micro_sequence_rotation_wraps_step_nodes() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern,
        rootNode,
        4
    );
    assert(sequence.ok);

    auto* graph = state.pattern.graph.get();
    assert(graph != nullptr);
    const auto first = graph->sequences[sequence.id].firstStepNode;
    assert(core::state::sequencer::setNodeNoteOffset(state.pattern, first + 0U, 1));
    assert(core::state::sequencer::setNodeNoteOffset(state.pattern, first + 3U, 7));

    assert(core::state::sequencer::rotateMicroSequenceSteps(state.pattern, sequence.id, 1));

    graph = state.pattern.graph.get();
    assert(graph != nullptr);
    assert(graph->sequences[sequence.id].offset == 0);
    assert(graph->stepNodes[first + 0U].noteOffset == 7);
    assert(graph->stepNodes[first + 1U].noteOffset == 1);
    assert(!graph->stepNodes[first + 2U].has(STEP_NODE_NOTE_OFFSET));
    assert(!graph->stepNodes[first + 3U].has(STEP_NODE_NOTE_OFFSET));

    std::cout << "[PASS] test_micro_sequence_rotation_wraps_step_nodes\n";
}

void test_cycle_state_rotation_wraps_state_nodes() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto cycleSet = core::state::sequencer::createCycleStateSet(
        state.pattern,
        rootNode,
        4
    );
    assert(cycleSet.ok);

    auto* graph = state.pattern.graph.get();
    assert(graph != nullptr);
    const auto first = graph->cycleSets[cycleSet.id].firstStateNode;
    assert(core::state::sequencer::setNodeNoteOffset(state.pattern, first + 0U, 2));
    assert(core::state::sequencer::setNodeNoteOffset(state.pattern, first + 2U, 5));

    assert(core::state::sequencer::rotateCycleStateSetSteps(state.pattern, cycleSet.id, -1));

    graph = state.pattern.graph.get();
    assert(graph != nullptr);
    assert(graph->cycleSets[cycleSet.id].offset == 0);
    assert(graph->stepNodes[first + 1U].noteOffset == 5);
    assert(graph->stepNodes[first + 3U].noteOffset == 2);
    assert(!graph->stepNodes[first + 0U].has(STEP_NODE_NOTE_OFFSET));
    assert(!graph->stepNodes[first + 2U].has(STEP_NODE_NOTE_OFFSET));

    std::cout << "[PASS] test_cycle_state_rotation_wraps_state_nodes\n";
}

void test_root_pattern_rotation_wraps_graph_step_nodes() {
    SequencerState state;
    state.pattern.length.set(4);
    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern,
        core::state::sequencer::rootStepNodeId(0),
        2
    );
    assert(sequence.ok);

    assert(core::state::sequencer::rotatePattern(state, 1));

    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    assert(!graph->stepNodes[0].has(STEP_NODE_CHILD_SEQUENCE));
    assert(graph->stepNodes[1].has(STEP_NODE_CHILD_SEQUENCE));
    assert(graph->stepNodes[1].childSequenceId == sequence.id);

    std::cout << "[PASS] test_root_pattern_rotation_wraps_graph_step_nodes\n";
}

void test_clear_node_children_detaches_links_and_bumps_revision_once() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern,
        rootNode,
        2
    );
    assert(sequence.ok);
    const auto cycleSet = core::state::sequencer::createCycleStateSet(
        state.pattern,
        rootNode,
        2
    );
    assert(cycleSet.ok);

    const uint32_t revisionBeforeClear = state.pattern.graphRevision.get();
    assert(core::state::sequencer::clearNodeChildren(state.pattern, rootNode));
    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    assert(!graph->stepNodes[rootNode].has(STEP_NODE_CHILD_SEQUENCE));
    assert(!graph->stepNodes[rootNode].has(STEP_NODE_CYCLE_SET));
    assert(graph->stepNodes[rootNode].childSequenceId == StepSequencerGraphLimits::INVALID_ID);
    assert(graph->stepNodes[rootNode].cycleSetId == StepSequencerGraphLimits::INVALID_ID);
    assert(state.pattern.graphRevision.get() == revisionBeforeClear + 1U);

    assert(!core::state::sequencer::clearNodeChildren(state.pattern, rootNode));
    assert(state.pattern.graphRevision.get() == revisionBeforeClear + 1U);

    std::cout << "[PASS] test_clear_node_children_detaches_links_and_bumps_revision_once\n";
}

void test_compact_graph_reclaims_detached_child_content() {
    SequencerState state;
    const auto firstRoot = core::state::sequencer::rootStepNodeId(0);
    const auto secondRoot = core::state::sequencer::rootStepNodeId(1);

    const auto first = core::state::sequencer::createMicroSequence(
        state.pattern,
        firstRoot,
        2
    );
    assert(first.ok);
    const auto second = core::state::sequencer::createMicroSequence(
        state.pattern,
        secondRoot,
        2
    );
    assert(second.ok);

    auto* graph = state.pattern.graph.get();
    assert(graph != nullptr);
    const uint16_t expandedCount = graph->stepNodeCount;
    assert(second.id > first.id);

    const auto* secondSequence = graph->sequence(second.id);
    assert(secondSequence != nullptr);
    const uint16_t secondChildNode = secondSequence->firstStepNode;
    assert(core::state::sequencer::setNodeNoteOffset(state.pattern, secondChildNode, 5));

    assert(core::state::sequencer::clearNodeChildSequence(state.pattern, firstRoot));
    graph = state.pattern.graph.get();
    assert(graph != nullptr);
    assert(graph->stepNodeCount == expandedCount);
    assert(graph->sequenceCount == 3);

    core::state::sequencer::SequencerGraphCompactionRemap remap;
    const auto result = core::state::sequencer::compactGraph(state.pattern, &remap);
    assert(result.ok);
    assert(result.compacted);

    graph = state.pattern.graph.get();
    assert(graph != nullptr);
    assert(graph->sequenceCount == 2);
    assert(graph->stepNodeCount ==
           SequencerState::MAX_STEPS +
               StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);
    assert(remap.sequence(second.id) == 1);
    assert(graph->stepNodes[secondRoot].childSequenceId == 1);

    const auto* compactedSequence = graph->sequence(1);
    assert(compactedSequence != nullptr);
    const auto* compactedChild = graph->stepNode(compactedSequence->firstStepNode);
    assert(compactedChild != nullptr);
    assert(compactedChild->has(STEP_NODE_NOTE_OFFSET));
    assert(compactedChild->noteOffset == 5);

    const auto secondRun = core::state::sequencer::compactGraph(state.pattern);
    assert(secondRun.ok);
    assert(!secondRun.compacted);

    std::cout << "[PASS] test_compact_graph_reclaims_detached_child_content\n";
}

void test_copy_node_children_remaps_nested_graph_content() {
    SequencerState source;
    const auto sourceRoot = core::state::sequencer::rootStepNodeId(0);
    const auto sourceMicro = core::state::sequencer::createMicroSequence(
        source.pattern,
        sourceRoot,
        2
    );
    assert(sourceMicro.ok);
    const auto sourceCycle = core::state::sequencer::createCycleStateSet(
        source.pattern,
        sourceRoot,
        2
    );
    assert(sourceCycle.ok);

    const auto* sourceGraph = core::state::sequencer::graphView(source.pattern);
    assert(sourceGraph != nullptr);
    const auto sourceMicroNode = sourceGraph->sequences[sourceMicro.id].firstStepNode;
    assert(core::state::sequencer::setNodeNoteOffset(source.pattern, sourceMicroNode, 3));
    const auto nestedCycle = core::state::sequencer::createCycleStateSet(
        source.pattern,
        sourceMicroNode,
        2
    );
    assert(nestedCycle.ok);
    assert(core::state::sequencer::setNodeVelocityOffset(
        source.pattern,
        sourceGraph->cycleSets[nestedCycle.id].firstStateNode,
        12
    ));

    SequencerState target;
    const auto targetRoot = core::state::sequencer::rootStepNodeId(3);
    assert(core::state::sequencer::copyNodeChildrenFromGraph(
        target.pattern,
        targetRoot,
        *sourceGraph,
        sourceRoot
    ));

    const auto* targetGraph = core::state::sequencer::graphView(target.pattern);
    assert(targetGraph != nullptr);
    const auto& targetNode = targetGraph->stepNodes[targetRoot];
    assert(targetNode.has(STEP_NODE_CHILD_SEQUENCE));
    assert(targetNode.has(STEP_NODE_CYCLE_SET));

    const auto* targetMicro = targetGraph->sequence(targetNode.childSequenceId);
    assert(targetMicro != nullptr);
    assert(targetMicro->length == 2);
    const auto& copiedMicroNode = targetGraph->stepNodes[targetMicro->firstStepNode];
    assert(copiedMicroNode.has(STEP_NODE_NOTE_OFFSET));
    assert(copiedMicroNode.noteOffset == 3);
    assert(copiedMicroNode.has(STEP_NODE_CYCLE_SET));

    const auto* copiedNestedCycle = targetGraph->cycleSet(copiedMicroNode.cycleSetId);
    assert(copiedNestedCycle != nullptr);
    assert(targetGraph->stepNodes[copiedNestedCycle->firstStateNode].velocityOffset == 12);

    std::cout << "[PASS] test_copy_node_children_remaps_nested_graph_content\n";
}

void test_clear_graph_releases_allocation_and_bumps_revision_once() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    assert(core::state::sequencer::createMicroSequence(state.pattern, rootNode, 2).ok);

    const uint32_t revisionBeforeClear = state.pattern.graphRevision.get();
    core::state::sequencer::clearGraph(state.pattern);
    assert(state.pattern.graph.get() == nullptr);
    assert(core::state::sequencer::graphView(state.pattern) == nullptr);
    assert(state.pattern.graphRevision.get() == revisionBeforeClear + 1U);

    core::state::sequencer::clearGraph(state.pattern);
    assert(state.pattern.graphRevision.get() == revisionBeforeClear + 1U);

    std::cout << "[PASS] test_clear_graph_releases_allocation_and_bumps_revision_once\n";
}

void test_graph_limits_are_reported() {
    SequencerState state;

    uint8_t created = 0;
    for (; created < SequencerState::MAX_STEPS; ++created) {
        const auto result = core::state::sequencer::createMicroSequence(
            state.pattern,
            core::state::sequencer::rootStepNodeId(created),
            1
        );
        if (!result.ok) {
            assert(result.limitReached);
            break;
        }
    }
    assert(created > 0);

    const auto rejected = core::state::sequencer::createMicroSequence(
        state.pattern,
        core::state::sequencer::rootStepNodeId(created),
        1
    );
    assert(!rejected.ok);
    assert(rejected.limitReached);

    std::cout << "[PASS] test_graph_limits_are_reported\n";
}

void test_local_variation_ranges_are_per_node_and_clamped() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);

    assert(core::state::sequencer::graphView(state.pattern) == nullptr);
    assert(!core::state::sequencer::setNodeLocalVariationRange(
        state.pattern,
        rootNode,
        core::state::sequencer::StepProperty::NOTE,
        0
    ));
    assert(core::state::sequencer::graphView(state.pattern) == nullptr);

    assert(core::state::sequencer::setNodeLocalVariationRange(
        state.pattern,
        rootNode,
        core::state::sequencer::StepProperty::NOTE,
        200
    ));
    const uint32_t revisionAfterPitch = state.pattern.graphRevision.get();
    assert(!core::state::sequencer::setNodeLocalVariationRange(
        state.pattern,
        rootNode,
        core::state::sequencer::StepProperty::NOTE,
        255
    ));
    assert(state.pattern.graphRevision.get() == revisionAfterPitch);

    assert(core::state::sequencer::setNodeLocalVariationRange(
        state.pattern,
        rootNode,
        core::state::sequencer::StepProperty::VELOCITY,
        250
    ));
    assert(core::state::sequencer::setNodeLocalVariationRange(
        state.pattern,
        rootNode,
        core::state::sequencer::StepProperty::GATE,
        150
    ));
    assert(core::state::sequencer::setNodeLocalVariationRange(
        state.pattern,
        rootNode,
        core::state::sequencer::StepProperty::NUDGE,
        90
    ));
    assert(!core::state::sequencer::setNodeLocalVariationRange(
        state.pattern,
        rootNode,
        core::state::sequencer::StepProperty::PROBABILITY,
        50
    ));

    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(rootNode);
    assert(node != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node,
               core::state::sequencer::StepProperty::NOTE
           ) == 36);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node,
               core::state::sequencer::StepProperty::VELOCITY
           ) == 127);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node,
               core::state::sequencer::StepProperty::GATE
           ) == 100);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node,
               core::state::sequencer::StepProperty::NUDGE
           ) == 50);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node,
               core::state::sequencer::StepProperty::PROBABILITY
           ) == 0);

    std::cout << "[PASS] test_local_variation_ranges_are_per_node_and_clamped\n";
}

void test_chord_state_is_explicit_and_resettable_per_node() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);

    StepSequencerChordSpec spec{};
    spec.voiceCount = 99;
    spec.color = 99;
    spec.variant = 2;
    spec.spread = 99;
    spec.strum = 120;
    spec.velocityCurve = -80;
    assert(core::state::sequencer::setNodeChordSpec(state.pattern, rootNode, spec));

    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(rootNode);
    assert(node != nullptr);
    assert(node->has(STEP_NODE_CHORD_MODE));
    assert(node->has(STEP_NODE_CHORD_LOCAL));
    assert(node->chordMode == StepSequencerChordMode::Local);
    assert(node->chordSpec.voiceCount == StepSequencerChordSpec::MAX_VOICES);
    assert(node->chordSpec.color == StepSequencerChordSpec::MAX_COLOR);
    assert(node->chordSpec.variant == 2);
    assert(node->chordSpec.spread == StepSequencerChordSpec::MAX_SPREAD);
    assert(node->chordSpec.strum == StepSequencerChordSpec::MAX_STRUM);
    assert(node->chordSpec.velocityCurve == StepSequencerChordSpec::MIN_VELOCITY_CURVE);

    const uint32_t revisionAfterSpec = state.pattern.graphRevision.get();
    assert(!core::state::sequencer::setNodeChordSpec(state.pattern, rootNode, node->chordSpec));
    assert(state.pattern.graphRevision.get() == revisionAfterSpec);

    assert(core::state::sequencer::setNodeChordMode(
        state.pattern,
        rootNode,
        StepSequencerChordMode::Single
    ));
    graph = core::state::sequencer::graphView(state.pattern);
    node = graph->stepNode(rootNode);
    assert(node->has(STEP_NODE_CHORD_MODE));
    assert(node->has(STEP_NODE_CHORD_LOCAL));
    assert(node->chordMode == StepSequencerChordMode::Single);

    assert(core::state::sequencer::clearNodeChordState(state.pattern, rootNode));
    graph = core::state::sequencer::graphView(state.pattern);
    node = graph->stepNode(rootNode);
    assert(!node->has(STEP_NODE_CHORD_MODE));
    assert(!node->has(STEP_NODE_CHORD_LOCAL));
    assert(node->chordMode == StepSequencerChordMode::Single);
    assert(node->chordSpec.voiceCount == 3);

    std::cout << "[PASS] test_chord_state_is_explicit_and_resettable_per_node\n";
}

void test_runtime_telemetry_sync_copies_expanded_variation() {
    SequencerState target;
    StepSequencerRuntimeState runtime;
    oc::note::sequencer::StepSequencerResolvedVariation variation{};
    variation.stepIndex = 2;
    variation.triggered = true;
    variation.resolved.note = 67;
    variation.resolved.velocity = 91;
    variation.resolved.gate = 80;
    variation.resolved.nudge = 3;

    runtime.expandedVariationTelemetry.valid = true;
    runtime.expandedVariationTelemetry.rootStepIndex = 2;
    runtime.expandedVariationTelemetry.cycleIndex = 5;
    runtime.expandedVariationTelemetry.store(
        0,
        42,
        3,
        6,
        variation,
        StepSequencerChordSource::Local,
        1,
        3,
        4,
        false
    );

    core::sequencer::publishRuntimeTelemetry(target, runtime);

    assert(target.expandedVariationTelemetry.valid);
    assert(target.expandedVariationTelemetry.rootStepIndex == 2);
    assert(target.expandedVariationTelemetry.cycleIndex == 5);
    assert(target.expandedVariationTelemetry.count == 1);
    assert(target.expandedVariationTelemetry.nodeId[0] == 42);
    assert(target.expandedVariationTelemetry.localTick[0] == 3);
    assert(target.expandedVariationTelemetry.spanTicks[0] == 6);
    assert(target.expandedVariationTelemetry.variation[0].resolved.note == 67);
    assert(target.expandedVariationTelemetry.variation[0].resolved.velocity == 91);
    assert(target.expandedVariationTelemetry.chordSource[0] == StepSequencerChordSource::Local);
    assert(target.expandedVariationTelemetry.chordVoiceIndex[0] == 1);
    assert(target.expandedVariationTelemetry.chordVoiceCount[0] == 3);
    assert(target.expandedVariationTelemetry.chordInterval[0] == 4);
    assert(!target.expandedVariationTelemetry.chordIntervalUsesScaleDegrees[0]);

    std::cout << "[PASS] test_runtime_telemetry_sync_copies_expanded_variation\n";
}

void test_runtime_telemetry_sync_publishes_root_offset_for_expanded_substeps() {
    SequencerState target;
    StepSequencerRuntimeState runtime;
    oc::note::sequencer::StepSequencerResolvedVariation variation{};
    variation.stepIndex = 0;
    variation.triggered = true;
    variation.resolved.note = 64;

    runtime.playheadStep = 0;
    runtime.playheadStepTicks = 24;
    runtime.playheadStepTickOffset = 12;
    runtime.expandedVariationTelemetry.valid = true;
    runtime.expandedVariationTelemetry.rootStepIndex = 0;
    runtime.expandedVariationTelemetry.store(0, 41, 12, 12, variation);

    core::sequencer::publishRuntimeTelemetry(target, runtime);
    assert(target.playheadStepTickOffset.get() == 12);

    runtime.playheadStepTickOffset = 13;
    core::sequencer::publishRuntimeTelemetry(target, runtime);
    assert(target.playheadStepTickOffset.get() == 12);

    runtime.playheadStepTickOffset = 7;
    runtime.expandedVariationTelemetry.reset();
    core::sequencer::publishRuntimeTelemetry(target, runtime);
    assert(target.playheadStepTickOffset.get() == 0);

    std::cout << "[PASS] test_runtime_telemetry_sync_publishes_root_offset_for_expanded_substeps\n";
}

}  // namespace

int main() {
    test_graph_root_is_allocated_once();
    test_pattern_without_graph_stays_unallocated_through_snapshot_copy();
    test_micro_sequence_exports_to_open_control_graph();
    test_step_node_child_presence_helpers_validate_targets();
    test_pattern_copy_preserves_graph();
    test_runtime_signature_tracks_graph_revision();
    test_create_micro_sequence_reuses_existing_child();
    test_cycle_state_set_resizes_to_reserved_capacity();
    test_micro_sequence_rotation_wraps_step_nodes();
    test_cycle_state_rotation_wraps_state_nodes();
    test_root_pattern_rotation_wraps_graph_step_nodes();
    test_clear_node_children_detaches_links_and_bumps_revision_once();
    test_compact_graph_reclaims_detached_child_content();
    test_copy_node_children_remaps_nested_graph_content();
    test_clear_graph_releases_allocation_and_bumps_revision_once();
    test_graph_limits_are_reported();
    test_local_variation_ranges_are_per_node_and_clamped();
    test_chord_state_is_explicit_and_resettable_per_node();
    test_runtime_telemetry_sync_copies_expanded_variation();
    test_runtime_telemetry_sync_publishes_root_offset_for_expanded_substeps();

    std::cout << "All SequencerGraphOps tests passed\n";
    return 0;
}

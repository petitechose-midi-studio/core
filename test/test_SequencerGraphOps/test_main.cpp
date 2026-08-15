#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <cstdlib>

#include <iostream>
#include <limits>
#include <new>
#include <oc/note/sequencer/StepSequencerExpander.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/sequencer/SequencerRuntimeStateSync.hpp"
#include "../../src/state/sequencer/SequencerGraphAsset.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerSnapshotOps.hpp"

namespace allocation_trace {

bool enabled = false;
std::size_t count = 0U;

class Scope {
public:
    Scope() {
        count = 0U;
        enabled = true;
    }

    ~Scope() { enabled = false; }

    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
};

}  // namespace allocation_trace

void* operator new(std::size_t bytes) {
    if (allocation_trace::enabled) ++allocation_trace::count;
    if (void* memory = std::malloc(bytes)) return memory;
    throw std::bad_alloc{};
}

void* operator new[](std::size_t bytes) { return ::operator new(bytes); }

void operator delete(void* memory) noexcept { std::free(memory); }

void operator delete[](void* memory) noexcept { ::operator delete(memory); }

void operator delete(void* memory, std::size_t) noexcept { ::operator delete(memory); }

void operator delete[](void* memory, std::size_t) noexcept {
    ::operator delete[](memory);
}

namespace {

namespace seq = core::state::sequencer;

using core::state::sequencer::SequencerPatternSnapshot;
using core::state::sequencer::SequencerState;
using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CHORD_LOCAL;
using oc::note::sequencer::STEP_NODE_CHORD_MODE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE;
using oc::note::sequencer::StepBitMask128;
using oc::note::sequencer::StepSequencerChordMode;
using oc::note::sequencer::StepSequencerChordSource;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerExpander;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerRuntimeState;

uint64_t byteHash(const void* data, std::size_t size) {
    const auto* bytes = static_cast<const uint8_t*>(data);
    uint64_t hash = 1469598103934665603ULL;
    for (std::size_t index = 0; index < size; ++index) {
        hash ^= bytes[index];
        hash *= 1099511628211ULL;
    }
    return hash;
}

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

    assert(core::state::sequencer::copyPatternState(target.pattern, source.pattern));
    assert(target.pattern.graph.get() == nullptr);

    std::cout << "[PASS] test_pattern_without_graph_stays_unallocated_through_snapshot_copy\n";
}

void test_micro_sequence_exports_to_open_control_graph() {
    SequencerState state;
    state.pattern.setContentLength(4);
    state.pattern.note[0] = 60;
    enableStep(state, 0);

    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto sequence = core::state::sequencer::createMicroSequence(state.pattern, rootNode, 2);
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

    const auto expansion =
        StepSequencerExpander::expandRootStep(runtime, *graph, 0, 0, 24, 0, true);

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
    assert(!core::state::sequencer::stepNodeHasAnyChildContent(state.pattern, rootNode));

    const auto sequence = core::state::sequencer::createMicroSequence(state.pattern, rootNode, 2);
    assert(sequence.ok);
    assert(core::state::sequencer::stepNodeHasMicroSequence(state.pattern, rootNode));
    assert(!core::state::sequencer::stepNodeHasCycleStateSet(state.pattern, rootNode));
    assert(core::state::sequencer::stepNodeHasAnyChildContent(state.pattern, rootNode));

    const auto cycleSet = core::state::sequencer::createCycleStateSet(state.pattern, rootNode, 3);
    assert(cycleSet.ok);
    assert(core::state::sequencer::stepNodeHasMicroSequence(state.pattern, rootNode));
    assert(core::state::sequencer::stepNodeHasCycleStateSet(state.pattern, rootNode));
    assert(core::state::sequencer::stepNodeHasAnyChildContent(state.pattern, rootNode));

    assert(core::state::sequencer::clearNodeChildSequence(state.pattern, rootNode));
    assert(!core::state::sequencer::stepNodeHasMicroSequence(state.pattern, rootNode));
    assert(core::state::sequencer::stepNodeHasCycleStateSet(state.pattern, rootNode));
    assert(core::state::sequencer::stepNodeHasAnyChildContent(state.pattern, rootNode));

    assert(core::state::sequencer::clearNodeCycleStateSet(state.pattern, rootNode));
    assert(!core::state::sequencer::stepNodeHasMicroSequence(state.pattern, rootNode));
    assert(!core::state::sequencer::stepNodeHasCycleStateSet(state.pattern, rootNode));
    assert(!core::state::sequencer::stepNodeHasAnyChildContent(state.pattern, rootNode));

    std::cout << "[PASS] test_step_node_child_presence_helpers_validate_targets\n";
}

void test_pattern_copy_preserves_graph() {
    SequencerState source;
    const auto rootNode = core::state::sequencer::rootStepNodeId(3);
    const auto cycleSet = core::state::sequencer::createCycleStateSet(source.pattern, rootNode, 3);
    assert(cycleSet.ok);
    const auto* sourceGraph = core::state::sequencer::graphView(source.pattern);
    assert(sourceGraph != nullptr);
    assert(core::state::sequencer::setNodeEnabledOverride(
        source.pattern, sourceGraph->cycleSets[cycleSet.id].firstStateNode + 1, false));

    SequencerState target;
    assert(core::state::sequencer::copyPatternState(target.pattern, source.pattern));

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
    const auto sequence = core::state::sequencer::createMicroSequence(state.pattern, rootNode, 2);
    assert(sequence.ok);

    SequencerPatternSnapshot after;
    core::state::sequencer::captureSnapshot(state.pattern, after);
    auto afterSignature = core::sequencer::captureRuntimeStateSignature(after);

    assert(!beforeSignature.matches(afterSignature));
    assert(afterSignature.graphRevision == state.pattern.graphRevision.get());

    std::cout << "[PASS] test_runtime_signature_tracks_graph_revision\n";
}

void test_pattern_pitch_context_syncs_directly_without_graph_rewrite() {
    SequencerState state;
    assert(core::state::sequencer::ensureGraphRoot(state.pattern));
    const uint32_t graphRevision = state.pattern.graphRevision.get();

    SequencerPatternSnapshot followSnapshot;
    core::state::sequencer::captureSnapshot(state.pattern, followSnapshot);
    auto followSignature = core::sequencer::captureRuntimeStateSignature(followSnapshot);
    auto runtime = makeRuntime(followSnapshot);
    assert(followSignature.pitchFollowsScale);
    assert(runtime.pitchFollowsScale);

    assert(state.setPitchEditMode(core::state::sequencer::SequencerPitchEditMode::CHROMATIC));
    assert(state.pattern.graphRevision.get() == graphRevision);

    SequencerPatternSnapshot chromaticSnapshot;
    core::state::sequencer::captureSnapshot(state.pattern, chromaticSnapshot);
    const auto chromaticSignature =
        core::sequencer::captureRuntimeStateSignature(chromaticSnapshot);
    runtime = makeRuntime(chromaticSnapshot);

    assert(!chromaticSignature.pitchFollowsScale);
    assert(!runtime.pitchFollowsScale);
    assert(!followSignature.matches(chromaticSignature));

    std::cout << "[PASS] Pattern Pitch Context syncs without graph rewrite\n";
}

void test_create_micro_sequence_reuses_existing_child() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto first = core::state::sequencer::createMicroSequence(state.pattern, rootNode, 2);
    assert(first.ok);
    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    const uint16_t stepNodeCount = graph->stepNodeCount;

    const auto second = core::state::sequencer::createMicroSequence(state.pattern, rootNode, 4);
    assert(second.ok);
    assert(second.id == first.id);
    assert(core::state::sequencer::graphView(state.pattern)->stepNodeCount == stepNodeCount);

    std::cout << "[PASS] test_create_micro_sequence_reuses_existing_child\n";
}

void test_cycle_state_set_resizes_to_reserved_capacity() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);
    const auto cycleSet = core::state::sequencer::createCycleStateSet(state.pattern, rootNode, 4);
    assert(cycleSet.ok);

    auto* graph = state.pattern.graph.get();
    assert(graph != nullptr);
    const auto* set = graph->cycleSet(cycleSet.id);
    assert(set != nullptr);
    assert(set->length == 4);
    assert(graph->stepNodeCount ==
           SequencerState::MAX_STEPS + StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET);

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
    const auto sequence = core::state::sequencer::createMicroSequence(state.pattern, rootNode, 4);
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
    const auto cycleSet = core::state::sequencer::createCycleStateSet(state.pattern, rootNode, 4);
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
    state.pattern.setContentLength(4);
    const auto sequence = core::state::sequencer::createMicroSequence(
        state.pattern, core::state::sequencer::rootStepNodeId(0), 2);
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
    const auto sequence = core::state::sequencer::createMicroSequence(state.pattern, rootNode, 2);
    assert(sequence.ok);
    const auto cycleSet = core::state::sequencer::createCycleStateSet(state.pattern, rootNode, 2);
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

    const auto first = core::state::sequencer::createMicroSequence(state.pattern, firstRoot, 2);
    assert(first.ok);
    const auto second = core::state::sequencer::createMicroSequence(state.pattern, secondRoot, 2);
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
    const auto result = core::state::sequencer::compactGraph(state.pattern, remap);
    assert(result.ok);
    assert(result.compacted);

    graph = state.pattern.graph.get();
    assert(graph != nullptr);
    assert(graph->sequenceCount == 2);
    assert(graph->stepNodeCount ==
           SequencerState::MAX_STEPS + StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);
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

void test_reserved_compaction_preserves_owner_and_allocates_zero() {
    SequencerState state;
    const auto firstRoot = core::state::sequencer::rootStepNodeId(0);
    const auto secondRoot = core::state::sequencer::rootStepNodeId(1);
    const auto first = core::state::sequencer::createMicroSequence(state.pattern, firstRoot, 2);
    const auto second = core::state::sequencer::createMicroSequence(state.pattern, secondRoot, 2);
    assert(first.ok);
    assert(second.ok);
    assert(core::state::sequencer::clearNodeChildSequence(state.pattern, firstRoot));

    auto reserved = core::app::makeExtmemUnique<oc::note::sequencer::StepSequencerGraph>();
    assert(reserved);
    auto* const liveOwner = state.pattern.graph.get();
    assert(liveOwner != nullptr);

    core::state::sequencer::SequencerGraphCompactionRemap remap;
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        const auto result = core::state::sequencer::compactGraphUsingReservedStorage(
            state.pattern, *reserved, remap);
        assert(result.ok);
        assert(result.compacted);
        assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
    }

    assert(state.pattern.graph.get() == liveOwner);
    assert(liveOwner->sequenceCount == 2U);
    assert(liveOwner->stepNodeCount ==
           SequencerState::MAX_STEPS + StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);
    assert(liveOwner->stepNodes[secondRoot].childSequenceId == 1U);

    const auto revisionBeforeAliasAttempt = state.pattern.graphRevision.get();
    const auto sequenceCountBeforeAliasAttempt = liveOwner->sequenceCount;
    const auto stepNodeCountBeforeAliasAttempt = liveOwner->stepNodeCount;
    const auto aliasResult =
        core::state::sequencer::compactGraphUsingReservedStorage(state.pattern, *liveOwner, remap);
    assert(!aliasResult.ok);
    assert(!aliasResult.compacted);
    assert(state.pattern.graph.get() == liveOwner);
    assert(state.pattern.graphRevision.get() == revisionBeforeAliasAttempt);
    assert(liveOwner->sequenceCount == sequenceCountBeforeAliasAttempt);
    assert(liveOwner->stepNodeCount == stepNodeCountBeforeAliasAttempt);

    std::cout << "[PASS] reserved compaction preserves owner and allocates zero\n";
}

void test_global_graph_validation_rejects_alias_cycle_and_overlap() {
    {
        StepSequencerGraph graph;
        assert(seq::initializeSequencerGraphRootUnversioned(graph));
        assert(seq::validInitializedSequencerGraph(graph));
        graph.stepNodes[0].flags = static_cast<uint16_t>(
            graph.stepNodes[0].flags | STEP_NODE_CHILD_SEQUENCE);
        graph.stepNodes[0].childSequenceId = 31U;
        const uint64_t malformedHash = byteHash(&graph, sizeof(graph));
        assert(!seq::validInitializedSequencerGraph(graph));
        assert(!seq::initializeSequencerGraphRootUnversioned(graph));
        assert(byteHash(&graph, sizeof(graph)) == malformedHash);
    }

    {
        SequencerState state;
        const auto sequence = seq::createMicroSequence(
            state.pattern, seq::rootStepNodeId(0U), 2U);
        assert(sequence.ok);
        auto* graph = state.pattern.graph.get();
        assert(graph != nullptr);
        graph->stepNodes[1].flags = static_cast<uint16_t>(
            graph->stepNodes[1].flags | STEP_NODE_CHILD_SEQUENCE);
        graph->stepNodes[1].childSequenceId = sequence.id;
        assert(!seq::validInitializedSequencerGraph(*graph));

        auto reserved = core::app::makeExtmemUnique<StepSequencerGraph>();
        assert(reserved);
        const uint64_t liveHash = byteHash(graph, sizeof(*graph));
        const uint32_t revision = state.pattern.graphRevision.get();
        seq::SequencerGraphCompactionRemap remap;
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            const auto result = seq::compactGraphUsingReservedStorage(
                state.pattern, *reserved, remap);
            assert(!result.ok && !result.compacted);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }
        assert(byteHash(graph, sizeof(*graph)) == liveHash);
        assert(state.pattern.graphRevision.get() == revision);
    }

    {
        SequencerState state;
        const auto outer = seq::createMicroSequence(
            state.pattern, seq::rootStepNodeId(0U), 2U);
        assert(outer.ok);
        const auto* graph = seq::graphView(state.pattern);
        assert(graph != nullptr);
        const uint16_t outerNode = graph->sequences[outer.id].firstStepNode;
        const auto inner = seq::createMicroSequence(
            state.pattern, outerNode, 2U);
        assert(inner.ok);
        graph = seq::graphView(state.pattern);
        assert(graph != nullptr);
        const uint16_t innerNode = graph->sequences[inner.id].firstStepNode;
        auto* mutableGraph = state.pattern.graph.get();
        mutableGraph->stepNodes[innerNode].flags = static_cast<uint16_t>(
            mutableGraph->stepNodes[innerNode].flags |
            STEP_NODE_CHILD_SEQUENCE);
        mutableGraph->stepNodes[innerNode].childSequenceId = outer.id;
        assert(!seq::validInitializedSequencerGraph(*mutableGraph));
    }

    {
        SequencerState state;
        const auto sequence = seq::createMicroSequence(
            state.pattern, seq::rootStepNodeId(0U), 2U);
        const auto cycleSet = seq::createCycleStateSet(
            state.pattern, seq::rootStepNodeId(1U), 2U);
        assert(sequence.ok && cycleSet.ok);
        auto* graph = state.pattern.graph.get();
        assert(graph != nullptr);
        graph->cycleSets[cycleSet.id].firstStateNode =
            graph->sequences[sequence.id].firstStepNode;
        assert(!seq::validInitializedSequencerGraph(*graph));
    }

    {
        SequencerState state;
        const auto detached = seq::createMicroSequence(
            state.pattern, seq::rootStepNodeId(0U), 2U);
        assert(detached.ok);
        assert(seq::clearNodeChildSequence(
            state.pattern, seq::rootStepNodeId(0U)));
        auto* graph = state.pattern.graph.get();
        assert(graph != nullptr);
        graph->sequences[detached.id].firstStepNode =
            StepSequencerGraphLimits::INVALID_ID;
        assert(seq::validInitializedSequencerGraph(*graph));
    }

    std::cout <<
        "[PASS] global Graph validation rejects alias/cycle/overlap safely\n";
}

void test_versioned_graph_copy_wrappers_prevalidate_and_publish_once() {
    using CopyFn = bool (*)(
        seq::SequencerPatternState&,
        seq::SequencerGraphNodeId,
        const StepSequencerGraph&,
        seq::SequencerGraphNodeId);
    constexpr std::array<CopyFn, 4U> copyFunctions{
        &seq::copyStepNodePayloadFromGraph,
        &seq::copyNodeChildrenFromGraph,
        &seq::copyNodeChildSequenceFromGraph,
        &seq::copyNodeCycleStateSetFromGraph,
    };

    SequencerState source;
    const auto sequence = seq::createMicroSequence(
        source.pattern, seq::rootStepNodeId(0U), 2U);
    const auto cycleSet = seq::createCycleStateSet(
        source.pattern, seq::rootStepNodeId(0U), 2U);
    assert(sequence.ok && cycleSet.ok);
    const auto* sourceGraph = seq::graphView(source.pattern);
    assert(sourceGraph != nullptr);
    assert(seq::validInitializedSequencerGraph(*sourceGraph));

    for (const auto copy : copyFunctions) {
        SequencerState invalidTarget;
        const uint32_t invalidRevision =
            invalidTarget.pattern.graphRevision.get();
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(!copy(
                invalidTarget.pattern,
                StepSequencerGraphLimits::INVALID_ID,
                *sourceGraph,
                seq::rootStepNodeId(0U)));
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }
        assert(invalidTarget.pattern.graph == nullptr);
        assert(invalidTarget.pattern.graphRevision.get() == invalidRevision);

        SequencerState graphlessTarget;
        const uint32_t graphlessRevision =
            graphlessTarget.pattern.graphRevision.get();
        assert(copy(
            graphlessTarget.pattern,
            seq::rootStepNodeId(0U),
            *sourceGraph,
            seq::rootStepNodeId(0U)));
        assert(graphlessTarget.pattern.graph != nullptr);
        assert(seq::validInitializedSequencerGraph(
            *graphlessTarget.pattern.graph));
        assert(
            graphlessTarget.pattern.graphRevision.get() ==
            graphlessRevision + 1U);

        SequencerState disabledTarget;
        disabledTarget.pattern.graph =
            core::app::makeExtmemUnique<StepSequencerGraph>();
        assert(disabledTarget.pattern.graph != nullptr);
        auto* disabledIdentity = disabledTarget.pattern.graph.get();
        assert(seq::isCanonicalDisabledSequencerGraph(*disabledIdentity));
        const uint32_t disabledRevision =
            disabledTarget.pattern.graphRevision.get();
        assert(copy(
            disabledTarget.pattern,
            seq::rootStepNodeId(0U),
            *sourceGraph,
            seq::rootStepNodeId(0U)));
        assert(disabledTarget.pattern.graph.get() == disabledIdentity);
        assert(seq::validInitializedSequencerGraph(*disabledIdentity));
        assert(
            disabledTarget.pattern.graphRevision.get() ==
            disabledRevision + 1U);

        SequencerState malformedTarget;
        assert(seq::ensureGraphRoot(malformedTarget.pattern));
        auto* malformedGraph = malformedTarget.pattern.graph.get();
        assert(malformedGraph != nullptr);
        malformedGraph->stepNodes[0].flags = static_cast<uint16_t>(
            malformedGraph->stepNodes[0].flags |
            STEP_NODE_CHILD_SEQUENCE);
        malformedGraph->stepNodes[0].childSequenceId = 31U;
        const uint64_t malformedHash =
            byteHash(malformedGraph, sizeof(*malformedGraph));
        const uint32_t malformedRevision =
            malformedTarget.pattern.graphRevision.get();
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            assert(!copy(
                malformedTarget.pattern,
                seq::rootStepNodeId(0U),
                *sourceGraph,
                seq::rootStepNodeId(0U)));
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }
        assert(byteHash(malformedGraph, sizeof(*malformedGraph)) ==
               malformedHash);
        assert(malformedTarget.pattern.graphRevision.get() ==
               malformedRevision);
    }

    std::cout <<
        "[PASS] versioned Graph copy wrappers prevalidate and publish once\n";
}

void test_versioned_graph_copy_accepts_compact_asset_source() {
    namespace seq = core::state::sequencer;

    SequencerState source;
    const auto sequence = seq::createMicroSequence(
        source.pattern, seq::rootStepNodeId(0U), 2U);
    assert(sequence.ok);
    auto* sourceGraph = source.pattern.graph.get();
    assert(sourceGraph != nullptr);
    const uint16_t child =
        sourceGraph->sequences[sequence.id].firstStepNode;
    assert(seq::setNodeNoteOffset(source.pattern, child, 7));

    seq::SequencerStepGraphPreset preset;
    seq::SequencerGraphAssetReport report;
    assert(seq::captureStepGraphPreset(
        source,
        0U,
        oc::note::sequencer::StepSequencerScaleSettings{},
        preset,
        &report));
    assert(report.ok());
    assert(preset.graph.rootSequenceId ==
           StepSequencerGraphLimits::INVALID_ID);
    assert(preset.graph.stepNodeCount < SequencerState::MAX_STEPS);
    assert(seq::stepGraphPresetGraphIsCanonical(preset.graph));

    SequencerState target;
    assert(seq::copyStepNodePayloadFromGraph(
        target.pattern,
        seq::rootStepNodeId(0U),
        preset.graph,
        seq::SequencerStepGraphPreset::ASSET_ROOT_NODE_ID));
    const auto* targetGraph = seq::graphView(target.pattern);
    assert(targetGraph != nullptr);
    const auto comparison = seq::compareSequencerGraphPayloads(
        preset.graph,
        seq::SequencerStepGraphPreset::ASSET_ROOT_NODE_ID,
        *targetGraph,
        seq::rootStepNodeId(0U),
        0U);
    assert(comparison.ok());
    assert(comparison.same);

    const uint64_t targetHash = byteHash(targetGraph, sizeof(*targetGraph));
    const uint32_t targetRevision = target.pattern.graphRevision.get();
    assert(!seq::copyStepNodePayloadFromGraph(
        target.pattern,
        seq::rootStepNodeId(0U),
        *targetGraph,
        seq::rootStepNodeId(0U)));
    assert(byteHash(targetGraph, sizeof(*targetGraph)) == targetHash);
    assert(target.pattern.graphRevision.get() == targetRevision);

    std::cout <<
        "[PASS] versioned Graph copy accepts compact asset source\n";
}

void test_child_extension_preserves_logical_nodes_across_offsets() {
    using StepNode = oc::note::sequencer::StepSequencerStepNode;

    const auto physicalIndex = [](uint8_t logical, int8_t offset, uint8_t length) {
        int index = static_cast<int>(logical) - static_cast<int>(offset);
        index %= static_cast<int>(length);
        if (index < 0) index += length;
        return static_cast<uint8_t>(index);
    };

    const auto assertCanonicalNewNode = [](const StepNode& node) {
        StepNode expected{};
        expected.flags = STEP_NODE_ENABLED_OVERRIDE;
        assert(byteHash(&node, sizeof(node)) ==
               byteHash(&expected, sizeof(expected)));
    };

    const auto verifyMicroSequence = [&](int8_t offset, uint8_t newLength) {
        constexpr uint8_t oldLength = 4U;
        SequencerState state;
        const auto created = seq::createMicroSequence(
            state.pattern, seq::rootStepNodeId(0U), oldLength);
        assert(created.ok);
        assert(seq::setMicroSequenceOffset(
            state.pattern, created.id, offset));

        auto* graph = state.pattern.graph.get();
        assert(graph != nullptr);
        auto* sequence = graph->sequence(created.id);
        assert(sequence != nullptr);
        const uint16_t firstNode = sequence->firstStepNode;
        for (uint8_t physical = 0U; physical < oldLength; ++physical) {
            auto& node = graph->stepNodes[firstNode + physical];
            node.flags = STEP_NODE_NOTE_OFFSET;
            node.noteOffset = static_cast<int8_t>(physical + 1U);
        }
        const uint8_t nestedPhysical =
            physicalIndex(0U, offset, oldLength);
        const auto nested = seq::createCycleStateSet(
            state.pattern,
            static_cast<uint16_t>(firstNode + nestedPhysical),
            2U);
        assert(nested.ok);
        graph = state.pattern.graph.get();
        sequence = graph->sequence(created.id);
        assert(sequence != nullptr);

        std::array<StepNode, oldLength> oldLogical{};
        for (uint8_t logical = 0U; logical < oldLength; ++logical) {
            oldLogical[logical] = graph->stepNodes[
                firstNode + physicalIndex(logical, offset, oldLength)];
        }
        for (uint8_t physical = oldLength; physical < newLength; ++physical) {
            auto& cold = graph->stepNodes[firstNode + physical];
            cold.flags = STEP_NODE_NOTE_OFFSET;
            cold.noteOffset = 63;
        }

        const uint32_t revision = state.pattern.graphRevision.get();
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            allocation_trace::Scope allocationScope;
            assert(seq::extendMicroSequencePreservingLogicalContentUnversioned(
                *graph, created.id, newLength));
            assert(allocation_trace::count == 0U);
            assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
        }
        assert(state.pattern.graphRevision.get() == revision);
        sequence = graph->sequence(created.id);
        assert(sequence != nullptr && sequence->length == newLength);
        assert(sequence->offset == offset);
        for (uint8_t logical = 0U; logical < oldLength; ++logical) {
            const auto& actual = graph->stepNodes[
                firstNode + physicalIndex(logical, offset, newLength)];
            assert(byteHash(&actual, sizeof(actual)) ==
                   byteHash(&oldLogical[logical], sizeof(oldLogical[logical])));
        }
        for (uint8_t logical = oldLength; logical < newLength; ++logical) {
            assertCanonicalNewNode(graph->stepNodes[
                firstNode + physicalIndex(logical, offset, newLength)]);
        }
        assert(seq::validInitializedSequencerGraph(*graph));
    };

    const auto verifyCycleSet = [&](int8_t offset, uint8_t newLength) {
        constexpr uint8_t oldLength = 4U;
        SequencerState state;
        const auto created = seq::createCycleStateSet(
            state.pattern, seq::rootStepNodeId(0U), oldLength);
        assert(created.ok);
        assert(seq::setCycleStateSetOffset(
            state.pattern, created.id, offset));

        auto* graph = state.pattern.graph.get();
        assert(graph != nullptr);
        auto* cycleSet = graph->cycleSet(created.id);
        assert(cycleSet != nullptr);
        const uint16_t firstNode = cycleSet->firstStateNode;
        for (uint8_t physical = 0U; physical < oldLength; ++physical) {
            auto& node = graph->stepNodes[firstNode + physical];
            node.flags = STEP_NODE_NOTE_OFFSET;
            node.noteOffset = static_cast<int8_t>(physical + 11U);
        }
        const uint8_t nestedPhysical =
            physicalIndex(1U, offset, oldLength);
        const auto nested = seq::createMicroSequence(
            state.pattern,
            static_cast<uint16_t>(firstNode + nestedPhysical),
            2U);
        assert(nested.ok);
        graph = state.pattern.graph.get();
        cycleSet = graph->cycleSet(created.id);
        assert(cycleSet != nullptr);

        std::array<StepNode, oldLength> oldLogical{};
        for (uint8_t logical = 0U; logical < oldLength; ++logical) {
            oldLogical[logical] = graph->stepNodes[
                firstNode + physicalIndex(logical, offset, oldLength)];
        }
        const uint32_t revision = state.pattern.graphRevision.get();
        assert(seq::extendCycleStateSetPreservingLogicalContentUnversioned(
            *graph, created.id, newLength));
        assert(state.pattern.graphRevision.get() == revision);
        cycleSet = graph->cycleSet(created.id);
        assert(cycleSet != nullptr && cycleSet->length == newLength);
        assert(cycleSet->offset == offset);
        for (uint8_t logical = 0U; logical < oldLength; ++logical) {
            const auto& actual = graph->stepNodes[
                firstNode + physicalIndex(logical, offset, newLength)];
            assert(byteHash(&actual, sizeof(actual)) ==
                   byteHash(&oldLogical[logical], sizeof(oldLogical[logical])));
        }
        for (uint8_t logical = oldLength; logical < newLength; ++logical) {
            assertCanonicalNewNode(graph->stepNodes[
                firstNode + physicalIndex(logical, offset, newLength)]);
        }
        assert(seq::validInitializedSequencerGraph(*graph));
    };

    verifyMicroSequence(3, 5U);   // positive overlap, copy backwards
    verifyCycleSet(-3, 5U);       // negative overlap, small growth
    verifyCycleSet(-1, 7U);       // negative offset within the growth gap

    std::cout <<
        "[PASS] child extension preserves logical nodes across offsets\n";
}

void test_copy_node_children_remaps_nested_graph_content() {
    SequencerState source;
    const auto sourceRoot = core::state::sequencer::rootStepNodeId(0);
    const auto sourceMicro =
        core::state::sequencer::createMicroSequence(source.pattern, sourceRoot, 2);
    assert(sourceMicro.ok);
    const auto sourceCycle =
        core::state::sequencer::createCycleStateSet(source.pattern, sourceRoot, 2);
    assert(sourceCycle.ok);

    const auto* sourceGraph = core::state::sequencer::graphView(source.pattern);
    assert(sourceGraph != nullptr);
    const auto sourceMicroNode = sourceGraph->sequences[sourceMicro.id].firstStepNode;
    assert(core::state::sequencer::setNodeNoteOffset(source.pattern, sourceMicroNode, 3));
    const auto nestedCycle =
        core::state::sequencer::createCycleStateSet(source.pattern, sourceMicroNode, 2);
    assert(nestedCycle.ok);
    assert(core::state::sequencer::setNodeVelocityOffset(
        source.pattern, sourceGraph->cycleSets[nestedCycle.id].firstStateNode, 12));

    SequencerState target;
    const auto targetRoot = core::state::sequencer::rootStepNodeId(3);
    assert(core::state::sequencer::copyNodeChildrenFromGraph(target.pattern, targetRoot,
                                                             *sourceGraph, sourceRoot));

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

void test_graph_payload_inspection_reports_exact_aggregate_budget() {
    SequencerState source;
    const auto root = core::state::sequencer::rootStepNodeId(0);
    const auto sequence =
        core::state::sequencer::createMicroSequence(source.pattern, root, 2);
    const auto cycle =
        core::state::sequencer::createCycleStateSet(source.pattern, root, 2);
    assert(sequence.ok);
    assert(cycle.ok);

    auto* graph = source.pattern.graph.get();
    assert(graph != nullptr);
    const uint16_t sequenceChild = graph->sequences[sequence.id].firstStepNode;
    const auto nested =
        core::state::sequencer::createCycleStateSet(source.pattern, sequenceChild, 2);
    assert(nested.ok);

    const auto inspection =
        core::state::sequencer::inspectSequencerGraphPayload(*graph, root, 0U);
    assert(inspection.ok());
    assert(inspection.payloadPresent);
    assert(inspection.budget.stepNodes == 48U);
    assert(inspection.budget.sequences == 1U);
    assert(inspection.budget.cycleSets == 2U);

    const auto emptyInspection =
        core::state::sequencer::inspectSequencerGraphPayload(*graph, 1U, 0U);
    assert(emptyInspection.ok());
    assert(!emptyInspection.payloadPresent);
    assert(emptyInspection.budget.stepNodes == 0U);
    assert(emptyInspection.budget.sequences == 0U);
    assert(emptyInspection.budget.cycleSets == 0U);

    std::cout << "[PASS] graph payload inspection reports exact aggregate budget\n";
}

void test_graph_copy_budget_is_overflow_safe_and_capacity_is_exact() {
    using core::state::sequencer::SequencerGraphCopyBudget;

    constexpr uint32_t max = std::numeric_limits<uint32_t>::max();
    SequencerGraphCopyBudget aggregate{
        .stepNodes = max - 2U,
        .sequences = max - 3U,
        .cycleSets = max - 4U,
    };
    assert(core::state::sequencer::appendSequencerGraphCopyBudget(
        aggregate, {.stepNodes = 2U, .sequences = 3U, .cycleSets = 4U}));
    assert(aggregate.stepNodes == max);
    assert(aggregate.sequences == max);
    assert(aggregate.cycleSets == max);

    const auto beforeOverflow = aggregate;
    assert(!core::state::sequencer::appendSequencerGraphCopyBudget(
        aggregate, {.stepNodes = 0U, .sequences = 1U, .cycleSets = 0U}));
    assert(aggregate.stepNodes == beforeOverflow.stepNodes);
    assert(aggregate.sequences == beforeOverflow.sequences);
    assert(aggregate.cycleSets == beforeOverflow.cycleSets);

    SequencerState target;
    assert(core::state::sequencer::ensureGraphRoot(target.pattern));
    auto* graph = target.pattern.graph.get();
    assert(graph != nullptr);
    const SequencerGraphCopyBudget exact{
        .stepNodes = static_cast<uint32_t>(graph->stepNodes.size() - graph->stepNodeCount),
        .sequences = static_cast<uint32_t>(graph->sequences.size() - graph->sequenceCount),
        .cycleSets = static_cast<uint32_t>(graph->cycleSets.size() - graph->cycleSetCount),
    };
    assert(core::state::sequencer::sequencerGraphHasCopyCapacity(*graph, exact));

    auto maxPlusOne = exact;
    ++maxPlusOne.stepNodes;
    assert(!core::state::sequencer::sequencerGraphHasCopyCapacity(*graph, maxPlusOne));
    maxPlusOne = exact;
    ++maxPlusOne.sequences;
    assert(!core::state::sequencer::sequencerGraphHasCopyCapacity(*graph, maxPlusOne));
    maxPlusOne = exact;
    ++maxPlusOne.cycleSets;
    assert(!core::state::sequencer::sequencerGraphHasCopyCapacity(*graph, maxPlusOne));

    auto disabled = core::app::makeExtmemUnique<StepSequencerGraph>();
    assert(disabled);
    assert(!core::state::sequencer::sequencerGraphHasCopyCapacity(*disabled, {}));
    graph->sequences[0].kind =
        oc::note::sequencer::StepSequencerSequenceKind::MicroSequence;
    assert(!core::state::sequencer::sequencerGraphHasCopyCapacity(*graph, {}));

    std::cout << "[PASS] graph copy budget is overflow-safe and capacity is exact\n";
}

void test_graph_payload_inspection_rejects_malformed_cycles_and_depth() {
    using core::state::sequencer::SequencerGraphPayloadInspectionStatus;

    SequencerState dangling;
    assert(core::state::sequencer::ensureGraphRoot(dangling.pattern));
    auto* graph = dangling.pattern.graph.get();
    assert(graph != nullptr);
    graph->stepNodes[0].flags = STEP_NODE_CHILD_SEQUENCE;
    graph->stepNodes[0].childSequenceId = StepSequencerGraphLimits::INVALID_ID;
    auto inspection =
        core::state::sequencer::inspectSequencerGraphPayload(*graph, 0U, 0U);
    assert(inspection.status == SequencerGraphPayloadInspectionStatus::MalformedGraph);

    SequencerState oversized;
    const auto sequence =
        core::state::sequencer::createMicroSequence(oversized.pattern, 0U, 2U);
    assert(sequence.ok);
    graph = oversized.pattern.graph.get();
    assert(graph != nullptr);
    ++graph->stepNodeCount;
    graph->sequences[sequence.id].length =
        StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP + 1U;
    inspection = core::state::sequencer::inspectSequencerGraphPayload(*graph, 0U, 0U);
    assert(inspection.status == SequencerGraphPayloadInspectionStatus::MalformedGraph);

    SequencerState cyclic;
    const auto cycle =
        core::state::sequencer::createCycleStateSet(cyclic.pattern, 0U, 1U);
    assert(cycle.ok);
    graph = cyclic.pattern.graph.get();
    assert(graph != nullptr);
    const uint16_t child = graph->cycleSets[cycle.id].firstStateNode;
    graph->stepNodes[child].flags = STEP_NODE_CYCLE_SET;
    graph->stepNodes[child].cycleSetId = cycle.id;
    inspection = core::state::sequencer::inspectSequencerGraphPayload(*graph, 0U, 0U);
    assert(inspection.status == SequencerGraphPayloadInspectionStatus::CycleDetected);

    SequencerState deep;
    uint16_t parent = 0U;
    for (uint8_t depth = 0U; depth < StepSequencerGraphLimits::MAX_DEPTH - 1U; ++depth) {
        const auto childSet =
            core::state::sequencer::createCycleStateSet(deep.pattern, parent, 1U);
        assert(childSet.ok);
        graph = deep.pattern.graph.get();
        parent = graph->cycleSets[childSet.id].firstStateNode;
    }
    inspection = core::state::sequencer::inspectSequencerGraphPayload(*graph, 0U, 0U);
    assert(inspection.ok());
    assert(inspection.budget.stepNodes == 48U);
    assert(inspection.budget.cycleSets == 3U);

    const auto tooDeep =
        core::state::sequencer::createCycleStateSet(deep.pattern, parent, 1U);
    assert(tooDeep.ok);
    inspection = core::state::sequencer::inspectSequencerGraphPayload(*graph, 0U, 0U);
    assert(inspection.status == SequencerGraphPayloadInspectionStatus::DepthExceeded);

    std::cout << "[PASS] graph payload inspection rejects malformed cycles and depth\n";
}

void test_canonical_disabled_proof_and_unversioned_root_initialization() {
    auto graph = core::app::makeExtmemUnique<StepSequencerGraph>();
    assert(graph);
    assert(core::state::sequencer::isCanonicalDisabledSequencerGraph(*graph));
    assert(core::state::sequencer::isDefaultSequencerGraphNodePayload(graph->stepNodes[0]));

    graph->stepNodes.back().childSequenceId = 7U;
    assert(core::state::sequencer::isDefaultSequencerGraphNodePayload(graph->stepNodes.back()));
    assert(!core::state::sequencer::isCanonicalDisabledSequencerGraph(*graph));
    graph->reset();
    graph->sequences.back().offset = 1;
    assert(!core::state::sequencer::isCanonicalDisabledSequencerGraph(*graph));
    graph->reset();
    graph->cycleSets.back().length = 1U;
    assert(!core::state::sequencer::isCanonicalDisabledSequencerGraph(*graph));
    graph->reset();

    SequencerState owner;
    owner.pattern.graph = std::move(graph);
    const uint32_t revision = owner.pattern.graphRevision.get();
    assert(core::state::sequencer::initializeSequencerGraphRootUnversioned(
        *owner.pattern.graph));
    assert(owner.pattern.graphRevision.get() == revision);
    assert(owner.pattern.graph->enabled);
    assert(owner.pattern.graph->rootSequenceId == 0U);
    assert(owner.pattern.graph->sequenceCount == 1U);
    assert(owner.pattern.graph->stepNodeCount == SequencerState::MAX_STEPS);
    assert(core::state::sequencer::initializeSequencerGraphRootUnversioned(
        *owner.pattern.graph));
    assert(owner.pattern.graphRevision.get() == revision);

    auto malformed = core::app::makeExtmemUnique<StepSequencerGraph>();
    assert(malformed);
    malformed->stepNodes.back().noteOffset = 1;
    assert(!core::state::sequencer::initializeSequencerGraphRootUnversioned(*malformed));
    assert(!malformed->enabled);
    assert(malformed->stepNodes.back().noteOffset == 1);

    std::cout << "[PASS] canonical disabled proof and unversioned root initialization\n";
}

void test_semantic_payload_comparison_ignores_physical_ids_and_validates() {
    using core::state::sequencer::SequencerGraphPayloadInspectionStatus;

    SequencerState source;
    const auto sourceSequence =
        core::state::sequencer::createMicroSequence(source.pattern, 0U, 2U);
    const auto sourceCycle =
        core::state::sequencer::createCycleStateSet(source.pattern, 0U, 2U);
    assert(sourceSequence.ok);
    assert(sourceCycle.ok);
    auto* sourceGraph = source.pattern.graph.get();
    assert(sourceGraph != nullptr);
    const uint16_t sourceChild = sourceGraph->sequences[sourceSequence.id].firstStepNode;
    assert(core::state::sequencer::setNodeNoteOffset(source.pattern, sourceChild, 5));

    SequencerState target;
    const auto detached =
        core::state::sequencer::createMicroSequence(target.pattern, 1U, 1U);
    assert(detached.ok);
    assert(core::state::sequencer::clearNodeChildSequence(target.pattern, 1U));
    assert(core::state::sequencer::copyStepNodePayloadFromGraph(
        target.pattern, 0U, *sourceGraph, 0U));
    auto* targetGraph = target.pattern.graph.get();
    assert(targetGraph != nullptr);
    assert(targetGraph->stepNodes[0].childSequenceId !=
           sourceGraph->stepNodes[0].childSequenceId);

    auto comparison = core::state::sequencer::compareSequencerGraphPayloads(
        *sourceGraph, 0U, *targetGraph, 0U, 0U);
    assert(comparison.ok());
    assert(comparison.same);

    const uint16_t targetChild =
        targetGraph->sequences[targetGraph->stepNodes[0].childSequenceId].firstStepNode;
    ++targetGraph->stepNodes[targetChild].noteOffset;
    comparison = core::state::sequencer::compareSequencerGraphPayloads(
        *sourceGraph, 0U, *targetGraph, 0U, 0U);
    assert(comparison.ok());
    assert(!comparison.same);

    targetGraph->stepNodes[0].flags = STEP_NODE_CHILD_SEQUENCE;
    targetGraph->stepNodes[0].childSequenceId = StepSequencerGraphLimits::INVALID_ID;
    comparison = core::state::sequencer::compareSequencerGraphPayloads(
        *sourceGraph, 0U, *targetGraph, 0U, 0U);
    assert(comparison.status == SequencerGraphPayloadInspectionStatus::MalformedGraph);
    assert(!comparison.same);

    std::cout << "[PASS] semantic payload comparison ignores IDs and validates\n";
}

void test_unversioned_copy_reset_and_resize_do_not_signal_revision() {
    SequencerState source;
    const auto sequence =
        core::state::sequencer::createMicroSequence(source.pattern, 0U, 2U);
    const auto cycle =
        core::state::sequencer::createCycleStateSet(source.pattern, 0U, 2U);
    assert(sequence.ok);
    assert(cycle.ok);
    auto* sourceGraph = source.pattern.graph.get();
    assert(sourceGraph != nullptr);
    const auto inspection =
        core::state::sequencer::inspectSequencerGraphPayload(*sourceGraph, 0U, 0U);
    assert(inspection.ok());

    SequencerState target;
    assert(core::state::sequencer::ensureGraphRoot(target.pattern));
    auto* targetGraph = target.pattern.graph.get();
    assert(targetGraph != nullptr);
    const uint32_t revision = target.pattern.graphRevision.get();
    const uint16_t nodeCount = targetGraph->stepNodeCount;
    const uint8_t sequenceCount = targetGraph->sequenceCount;
    const uint8_t cycleSetCount = targetGraph->cycleSetCount;
    assert(core::state::sequencer::copyStepNodePayloadFromGraphUnversioned(
        *targetGraph, 0U, *sourceGraph, 0U, 0U));
    assert(target.pattern.graphRevision.get() == revision);
    assert(targetGraph->stepNodeCount == nodeCount + inspection.budget.stepNodes);
    assert(targetGraph->sequenceCount == sequenceCount + inspection.budget.sequences);
    assert(targetGraph->cycleSetCount == cycleSetCount + inspection.budget.cycleSets);
    assert(core::state::sequencer::resetStepNodePayloadUnversioned(*targetGraph, 0U));
    assert(target.pattern.graphRevision.get() == revision);
    assert(!core::state::sequencer::resetStepNodePayloadUnversioned(*targetGraph, 0U));

    const uint16_t retainedSequenceNode = static_cast<uint16_t>(
        sourceGraph->sequences[sequence.id].firstStepNode + 15U);
    sourceGraph->stepNodes[retainedSequenceNode].flags = STEP_NODE_NOTE_OFFSET;
    sourceGraph->stepNodes[retainedSequenceNode].noteOffset = 9;
    const uint32_t sourceRevision = source.pattern.graphRevision.get();
    const uint8_t sequenceCapacity =
        core::state::sequencer::sequencerMicroSequenceReservedCapacity(
            *sourceGraph, sequence.id);
    assert(sequenceCapacity == 16U);
    assert(core::state::sequencer::sequencerMicroSequenceReservedCapacity(
               *sourceGraph, StepSequencerGraphLimits::INVALID_ID) == 0U);
    assert(core::state::sequencer::resizeMicroSequenceUnversioned(
        *sourceGraph, sequence.id, sequenceCapacity));
    assert(sourceGraph->stepNodes[retainedSequenceNode].noteOffset == 9);
    assert(source.pattern.graphRevision.get() == sourceRevision);
    assert(!core::state::sequencer::resizeMicroSequenceUnversioned(
        *sourceGraph, sequence.id, sequenceCapacity));
    assert(!core::state::sequencer::resizeMicroSequenceUnversioned(
        *sourceGraph, sequence.id, 0U));
    assert(!core::state::sequencer::resizeMicroSequenceUnversioned(
        *sourceGraph, sequence.id, static_cast<uint8_t>(sequenceCapacity + 1U)));

    const uint8_t cycleCapacity =
        core::state::sequencer::sequencerCycleStateSetReservedCapacity(
            *sourceGraph, cycle.id);
    assert(cycleCapacity == 16U);
    assert(core::state::sequencer::sequencerCycleStateSetReservedCapacity(
               *sourceGraph, StepSequencerGraphLimits::INVALID_ID) == 0U);
    assert(core::state::sequencer::resizeCycleStateSetUnversioned(
        *sourceGraph, cycle.id, cycleCapacity));
    assert(source.pattern.graphRevision.get() == sourceRevision);
    assert(!core::state::sequencer::resizeCycleStateSetUnversioned(
        *sourceGraph, cycle.id, cycleCapacity));
    assert(!core::state::sequencer::resizeCycleStateSetUnversioned(
        *sourceGraph, cycle.id, 0U));
    assert(!core::state::sequencer::resizeCycleStateSetUnversioned(
        *sourceGraph, cycle.id, static_cast<uint8_t>(cycleCapacity + 1U)));

    SequencerState versioned;
    const auto versionedSequence =
        core::state::sequencer::createMicroSequence(versioned.pattern, 0U, 2U);
    const auto versionedCycle =
        core::state::sequencer::createCycleStateSet(versioned.pattern, 0U, 2U);
    assert(versionedSequence.ok);
    assert(versionedCycle.ok);
    const uint32_t beforeSequenceResize = versioned.pattern.graphRevision.get();
    assert(core::state::sequencer::resizeMicroSequence(
        versioned.pattern, versionedSequence.id, 3U));
    assert(versioned.pattern.graphRevision.get() == beforeSequenceResize + 1U);
    assert(!core::state::sequencer::resizeMicroSequence(
        versioned.pattern, versionedSequence.id, 3U));
    assert(versioned.pattern.graphRevision.get() == beforeSequenceResize + 1U);
    const uint32_t beforeCycleResize = versioned.pattern.graphRevision.get();
    assert(core::state::sequencer::resizeCycleStateSet(
        versioned.pattern, versionedCycle.id, 3U));
    assert(versioned.pattern.graphRevision.get() == beforeCycleResize + 1U);

    SequencerState versionedCopy;
    assert(core::state::sequencer::ensureGraphRoot(versionedCopy.pattern));
    const uint32_t beforeCopy = versionedCopy.pattern.graphRevision.get();
    assert(core::state::sequencer::copyStepNodePayloadFromGraph(
        versionedCopy.pattern, 0U, *sourceGraph, 0U));
    assert(versionedCopy.pattern.graphRevision.get() == beforeCopy + 1U);

    std::cout << "[PASS] unversioned copy/reset/resize do not signal revision\n";
}

void test_graph_preflight_and_unversioned_mutations_allocate_zero() {
    using core::state::sequencer::SequencerGraphCopyBudget;

    SequencerState source;
    const auto sequence =
        core::state::sequencer::createMicroSequence(source.pattern, 0U, 2U);
    const auto cycle =
        core::state::sequencer::createCycleStateSet(source.pattern, 0U, 2U);
    assert(sequence.ok);
    assert(cycle.ok);
    auto* sourceGraph = source.pattern.graph.get();
    assert(sourceGraph != nullptr);

    SequencerState target;
    assert(core::state::sequencer::ensureGraphRoot(target.pattern));
    auto* targetGraph = target.pattern.graph.get();
    assert(targetGraph != nullptr);
    const uint32_t targetRevision = target.pattern.graphRevision.get();
    const uint32_t sourceRevision = source.pattern.graphRevision.get();

    auto disabled = core::app::makeExtmemUnique<StepSequencerGraph>();
    assert(disabled);
    SequencerGraphCopyBudget aggregate{};
    {
        core::app::testing::ScopedExtmemAllocationFailure failure(1U);
        allocation_trace::Scope allocationScope;

        const auto inspection =
            core::state::sequencer::inspectSequencerGraphPayload(*sourceGraph, 0U, 0U);
        assert(inspection.ok());
        assert(core::state::sequencer::appendSequencerGraphCopyBudget(
            aggregate, inspection.budget));
        assert(core::state::sequencer::sequencerGraphHasCopyCapacity(
            *targetGraph, aggregate));
        assert(core::state::sequencer::sequencerMicroSequenceReservedCapacity(
                   *sourceGraph, sequence.id) == 16U);
        assert(core::state::sequencer::sequencerCycleStateSetReservedCapacity(
                   *sourceGraph, cycle.id) == 16U);
        assert(core::state::sequencer::sequencerMicroSequenceReservedCapacity(
                   *disabled, 0U) == 0U);
        assert(core::state::sequencer::sequencerCycleStateSetReservedCapacity(
                   *disabled, 0U) == 0U);
        assert(core::state::sequencer::isCanonicalDisabledSequencerGraph(*disabled));
        assert(core::state::sequencer::initializeSequencerGraphRootUnversioned(*disabled));
        const auto comparison = core::state::sequencer::compareSequencerGraphPayloads(
            *sourceGraph, 0U, *sourceGraph, 0U, 0U);
        assert(comparison.ok());
        assert(comparison.same);
        assert(core::state::sequencer::copyStepNodePayloadFromGraphUnversioned(
            *targetGraph, 0U, *sourceGraph, 0U, 0U));
        assert(core::state::sequencer::resetStepNodePayloadUnversioned(*targetGraph, 0U));
        assert(core::state::sequencer::resizeMicroSequenceUnversioned(
            *sourceGraph, sequence.id, 3U));
        assert(core::state::sequencer::resizeCycleStateSetUnversioned(
            *sourceGraph, cycle.id, 3U));
        assert(core::app::testing::extmemAllocationFailureOrdinal == 1U);
    }

    assert(allocation_trace::count == 0U);
    assert(target.pattern.graphRevision.get() == targetRevision);
    assert(source.pattern.graphRevision.get() == sourceRevision);

    std::cout << "[PASS] graph preflight and unversioned mutations allocate zero\n";
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
            state.pattern, core::state::sequencer::rootStepNodeId(created), 1);
        if (!result.ok) {
            assert(result.limitReached);
            break;
        }
    }
    assert(created > 0);

    const auto rejected = core::state::sequencer::createMicroSequence(
        state.pattern, core::state::sequencer::rootStepNodeId(created), 1);
    assert(!rejected.ok);
    assert(rejected.limitReached);

    std::cout << "[PASS] test_graph_limits_are_reported\n";
}

void test_local_variation_ranges_are_per_node_and_clamped() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);

    assert(core::state::sequencer::graphView(state.pattern) == nullptr);
    assert(!core::state::sequencer::setNodeLocalVariationRange(
        state.pattern, rootNode, core::state::sequencer::StepProperty::NOTE, 0));
    assert(core::state::sequencer::graphView(state.pattern) == nullptr);

    assert(core::state::sequencer::setNodeLocalVariationRange(
        state.pattern, rootNode, core::state::sequencer::StepProperty::NOTE, 200));
    const uint32_t revisionAfterPitch = state.pattern.graphRevision.get();
    assert(!core::state::sequencer::setNodeLocalVariationRange(
        state.pattern, rootNode, core::state::sequencer::StepProperty::NOTE, 255));
    assert(state.pattern.graphRevision.get() == revisionAfterPitch);

    assert(core::state::sequencer::setNodeLocalVariationRange(
        state.pattern, rootNode, core::state::sequencer::StepProperty::VELOCITY, 250));
    assert(core::state::sequencer::setNodeLocalVariationRange(
        state.pattern, rootNode, core::state::sequencer::StepProperty::GATE, 150));
    assert(core::state::sequencer::setNodeLocalVariationRange(
        state.pattern, rootNode, core::state::sequencer::StepProperty::NUDGE, 90));
    assert(!core::state::sequencer::setNodeLocalVariationRange(
        state.pattern, rootNode, core::state::sequencer::StepProperty::PROBABILITY, 50));

    const auto* graph = core::state::sequencer::graphView(state.pattern);
    assert(graph != nullptr);
    const auto* node = graph->stepNode(rootNode);
    assert(node != nullptr);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node, core::state::sequencer::StepProperty::NOTE) == 36);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node, core::state::sequencer::StepProperty::VELOCITY) == 127);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node, core::state::sequencer::StepProperty::GATE) == 100);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node, core::state::sequencer::StepProperty::NUDGE) == 50);
    assert(core::state::sequencer::nodeLocalVariationRange(
               *node, core::state::sequencer::StepProperty::PROBABILITY) == 0);

    std::cout << "[PASS] test_local_variation_ranges_are_per_node_and_clamped\n";
}

void test_chord_state_is_explicit_and_resettable_per_node() {
    SequencerState state;
    const auto rootNode = core::state::sequencer::rootStepNodeId(0);

    StepSequencerChordSpec spec{};
    spec.voiceCount = 99;
    spec.harmonyData = 0xFFU;
    spec.voicingData = 2;
    spec.inversionData = 99;
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
    assert(node->chordSpec.harmony() ==
           oc::note::sequencer::StepSequencerChordHarmony::DiatonicTriad);
    assert(node->chordSpec.voicing() == oc::note::sequencer::StepSequencerChordVoicing::Wide);
    assert(node->chordSpec.inversion() == 3);
    assert(node->chordSpec.strum == StepSequencerChordSpec::MAX_STRUM);
    assert(node->chordSpec.velocityCurve == StepSequencerChordSpec::MIN_VELOCITY_CURVE);

    const uint32_t revisionAfterSpec = state.pattern.graphRevision.get();
    assert(!core::state::sequencer::setNodeChordSpec(state.pattern, rootNode, node->chordSpec));
    assert(state.pattern.graphRevision.get() == revisionAfterSpec);

    assert(core::state::sequencer::setNodeChordMode(state.pattern, rootNode,
                                                    StepSequencerChordMode::Single));
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
    runtime.expandedVariationTelemetry.requestedNoteCount = 17;
    runtime.expandedVariationTelemetry.noteBudgetExceeded = true;
    runtime.runtimeDiagnostics.noteBudgetExceeded = true;
    runtime.runtimeDiagnostics.noteBudgetExceededCount = 3;
    runtime.expandedVariationTelemetry.store(0, 42, 3, 6, variation,
                                             StepSequencerChordSource::Local, 1, 3, 4, false);

    core::sequencer::publishRuntimeTelemetry(target, runtime);

    assert(target.expandedVariationTelemetry.valid);
    assert(target.expandedVariationTelemetry.rootStepIndex == 2);
    assert(target.expandedVariationTelemetry.cycleIndex == 5);
    assert(target.expandedVariationTelemetry.count == 1);
    assert(target.expandedVariationTelemetry.requestedNoteCount == 17);
    assert(target.expandedVariationTelemetry.noteBudgetExceeded);
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
    assert(target.runtimeDiagnostics.noteBudgetExceeded);
    assert(target.runtimeDiagnostics.noteBudgetExceededCount == 3);

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

void test_runtime_visual_phase_is_smooth_bounded_and_ui_only() {
    using core::sequencer::projectPlaybackPhaseQ8;

    assert(projectPlaybackPhaseQ8(0U, 24U, 1000U, 1000U, 1500U) == 5U);
    assert(projectPlaybackPhaseQ8(12U, 24U, 1000U, 1000U, 1500U) == 133U);
    assert(projectPlaybackPhaseQ8(23U, 24U, 1000U, 1000U, 2500U) == 255U);
    assert(projectPlaybackPhaseQ8(12U, 24U, 0U, 0U, 0U) == 128U);

    SequencerState target;
    core::sequencer::SequencerRuntimeTelemetrySnapshot telemetry{};
    telemetry.playheadStep = 0;
    telemetry.playheadStepTicks = 24U;
    telemetry.playheadStepTickOffset = 12U;
    telemetry.playheadStepPhaseQ8 = 133U;
    core::sequencer::publishRuntimeTelemetry(target, telemetry);
    assert(target.playheadStepPhaseQ8.get() == 133U);
    // The raw scheduler offset keeps its existing publication policy.
    assert(target.playheadStepTickOffset.get() == 0U);

    telemetry.playheadStep = -1;
    telemetry.playheadStepPhaseQ8 = 200U;
    core::sequencer::publishRuntimeTelemetry(target, telemetry);
    assert(target.playheadStepPhaseQ8.get() == 0U);

    std::cout << "[PASS] test_runtime_visual_phase_is_smooth_bounded_and_ui_only\n";
}

}  // namespace

int main() {
    test_graph_root_is_allocated_once();
    test_pattern_without_graph_stays_unallocated_through_snapshot_copy();
    test_micro_sequence_exports_to_open_control_graph();
    test_step_node_child_presence_helpers_validate_targets();
    test_pattern_copy_preserves_graph();
    test_runtime_signature_tracks_graph_revision();
    test_pattern_pitch_context_syncs_directly_without_graph_rewrite();
    test_create_micro_sequence_reuses_existing_child();
    test_cycle_state_set_resizes_to_reserved_capacity();
    test_micro_sequence_rotation_wraps_step_nodes();
    test_cycle_state_rotation_wraps_state_nodes();
    test_root_pattern_rotation_wraps_graph_step_nodes();
    test_clear_node_children_detaches_links_and_bumps_revision_once();
    test_compact_graph_reclaims_detached_child_content();
    test_reserved_compaction_preserves_owner_and_allocates_zero();
    test_global_graph_validation_rejects_alias_cycle_and_overlap();
    test_versioned_graph_copy_wrappers_prevalidate_and_publish_once();
    test_versioned_graph_copy_accepts_compact_asset_source();
    test_child_extension_preserves_logical_nodes_across_offsets();
    test_copy_node_children_remaps_nested_graph_content();
    test_graph_payload_inspection_reports_exact_aggregate_budget();
    test_graph_copy_budget_is_overflow_safe_and_capacity_is_exact();
    test_graph_payload_inspection_rejects_malformed_cycles_and_depth();
    test_canonical_disabled_proof_and_unversioned_root_initialization();
    test_semantic_payload_comparison_ignores_physical_ids_and_validates();
    test_unversioned_copy_reset_and_resize_do_not_signal_revision();
    test_graph_preflight_and_unversioned_mutations_allocate_zero();
    test_clear_graph_releases_allocation_and_bumps_revision_once();
    test_graph_limits_are_reported();
    test_local_variation_ranges_are_per_node_and_clamped();
    test_chord_state_is_explicit_and_resettable_per_node();
    test_runtime_telemetry_sync_copies_expanded_variation();
    test_runtime_telemetry_sync_publishes_root_offset_for_expanded_substeps();
    test_runtime_visual_phase_is_smooth_bounded_and_ui_only();

    std::cout << "All SequencerGraphOps tests passed\n";
    return 0;
}

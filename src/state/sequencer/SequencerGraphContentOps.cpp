#include "state/sequencer/SequencerGraphOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOpsInternal.hpp"

namespace core::state::sequencer {

using namespace graph_ops_internal;

FLASHMEM SequencerGraphCreateResult createMicroSequence(
    SequencerPatternState& pattern,
    SequencerGraphNodeId parentNodeId,
    uint8_t length
) {
    if (!ensureGraphRoot(pattern)) {
        return {.ok = false, .limitReached = true};
    }
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, parentNodeId)) {
        return {.ok = false, .limitReached = true};
    }

    auto& parent = graph->stepNodes[parentNodeId];
    if (parent.has(STEP_NODE_CHILD_SEQUENCE)) {
        const auto* existing = graph->sequence(parent.childSequenceId);
        if (existing != nullptr) {
            return {.ok = true, .limitReached = false, .id = parent.childSequenceId};
        }
        parent.childSequenceId = kInvalidId;
        assignFlag(parent.flags, STEP_NODE_CHILD_SEQUENCE, false);
    }

    const uint16_t sequenceId = allocateSequence(
        *graph,
        StepSequencerSequenceKind::MicroSequence,
        length,
        StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP
    );
    if (sequenceId == kInvalidId) {
        return {.ok = false, .limitReached = true};
    }

    const auto* created = graph->sequence(sequenceId);
    if (created == nullptr) {
        return {.ok = false, .limitReached = true};
    }
    const uint8_t reserved = sequenceReservedCapacity(*graph, sequenceId);
    for (uint8_t i = 0; i < reserved; ++i) {
        (void)initializeNodePitchPolicy(
            graph->stepNodes[static_cast<uint16_t>(created->firstStepNode + i)],
            pattern.pitchEditMode
        );
    }

    parent.childSequenceId = sequenceId;
    parent.flags = static_cast<uint16_t>(parent.flags | STEP_NODE_CHILD_SEQUENCE);
    pattern.bumpGraphRevision();
    return {.ok = true, .limitReached = false, .id = sequenceId};
}

FLASHMEM bool resizeMicroSequence(
    SequencerPatternState& pattern,
    SequencerGraphSequenceId sequenceId,
    uint8_t length
) {
    if (length == 0 ||
        length > StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP) {
        return false;
    }
    if (!ensureGraphRoot(pattern)) return false;

    auto* graph = mutableGraph(pattern);
    if (graph == nullptr) return false;
    auto* sequence = mutableMicroSequence(*graph, sequenceId);
    if (sequence == nullptr) return false;
    if (length > sequenceReservedCapacity(*graph, sequenceId)) return false;
    if (sequence->length == length) return false;

    sequence->length = length;
    pattern.bumpGraphRevision();
    return true;
}

FLASHMEM bool resizeCycleStateSet(
    SequencerPatternState& pattern,
    SequencerGraphCycleSetId cycleSetId,
    uint8_t length
) {
    if (length == 0 ||
        length > StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET) {
        return false;
    }
    if (!ensureGraphRoot(pattern)) return false;

    auto* graph = mutableGraph(pattern);
    if (graph == nullptr) return false;
    auto* cycleSet = mutableCycleSet(*graph, cycleSetId);
    if (cycleSet == nullptr) return false;
    if (length > cycleSetReservedCapacity(*graph, cycleSetId)) return false;
    if (cycleSet->length == length) return false;

    cycleSet->length = length;
    pattern.bumpGraphRevision();
    return true;
}

FLASHMEM bool setMicroSequenceOffset(
    SequencerPatternState& pattern,
    SequencerGraphSequenceId sequenceId,
    int8_t offset
) {
    if (!ensureGraphRoot(pattern)) return false;

    auto* graph = mutableGraph(pattern);
    if (graph == nullptr) return false;
    auto* sequence = mutableMicroSequence(*graph, sequenceId);
    if (sequence == nullptr) return false;

    const int8_t normalized = normalizeContentOffset(offset, sequence->length);
    if (sequence->offset == normalized) return false;
    sequence->offset = normalized;
    pattern.bumpGraphRevision();
    return true;
}

FLASHMEM bool setCycleStateSetOffset(
    SequencerPatternState& pattern,
    SequencerGraphCycleSetId cycleSetId,
    int8_t offset
) {
    if (!ensureGraphRoot(pattern)) return false;

    auto* graph = mutableGraph(pattern);
    if (graph == nullptr) return false;
    auto* cycleSet = mutableCycleSet(*graph, cycleSetId);
    if (cycleSet == nullptr) return false;

    const int8_t normalized = normalizeContentOffset(offset, cycleSet->length);
    if (cycleSet->offset == normalized) return false;
    cycleSet->offset = normalized;
    pattern.bumpGraphRevision();
    return true;
}

FLASHMEM bool rotateRootStepNodes(SequencerPatternState& pattern, int offsetSteps) {
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !graph->enabled) return false;

    const auto* root = graph->sequence(graph->rootSequenceId);
    if (root == nullptr || root->kind != StepSequencerSequenceKind::RootPattern) {
        return false;
    }

    const uint8_t length = std::min<uint8_t>(
        pattern.length.get(),
        SequencerPatternState::MAX_STEPS
    );
    const bool changed = rotateStepNodeSegment(
        *graph,
        root->firstStepNode,
        length,
        offsetSteps,
        0
    );
    bump(pattern, changed);
    return changed;
}

FLASHMEM bool rotateMicroSequenceSteps(
    SequencerPatternState& pattern,
    SequencerGraphSequenceId sequenceId,
    int offsetSteps
) {
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !graph->enabled) return false;

    auto* sequence = mutableMicroSequence(*graph, sequenceId);
    if (sequence == nullptr) return false;

    const bool changed = rotateStepNodeSegment(
        *graph,
        sequence->firstStepNode,
        sequence->length,
        offsetSteps,
        sequence->offset
    );
    const bool offsetChanged = sequence->offset != 0;
    if (sequence->offset != 0) {
        sequence->offset = 0;
    }
    bump(pattern, changed || offsetChanged);
    return changed || offsetChanged;
}

FLASHMEM bool rotateCycleStateSetSteps(
    SequencerPatternState& pattern,
    SequencerGraphCycleSetId cycleSetId,
    int offsetSteps
) {
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !graph->enabled) return false;

    auto* cycleSet = mutableCycleSet(*graph, cycleSetId);
    if (cycleSet == nullptr) {
        return false;
    }

    const bool changed = rotateStepNodeSegment(
        *graph,
        cycleSet->firstStateNode,
        cycleSet->length,
        offsetSteps,
        cycleSet->offset
    );
    const bool offsetChanged = cycleSet->offset != 0;
    if (cycleSet->offset != 0) {
        cycleSet->offset = 0;
    }
    bump(pattern, changed || offsetChanged);
    return changed || offsetChanged;
}

FLASHMEM SequencerGraphCreateResult createCycleStateSet(
    SequencerPatternState& pattern,
    SequencerGraphNodeId parentNodeId,
    uint8_t length
) {
    if (!ensureGraphRoot(pattern)) {
        return {.ok = false, .limitReached = true};
    }
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, parentNodeId)) {
        return {.ok = false, .limitReached = true};
    }

    auto& parent = graph->stepNodes[parentNodeId];
    if (parent.has(STEP_NODE_CYCLE_SET)) {
        const auto* existing = graph->cycleSet(parent.cycleSetId);
        if (existing != nullptr) {
            return {.ok = true, .limitReached = false, .id = parent.cycleSetId};
        }
        parent.cycleSetId = kInvalidId;
        assignFlag(parent.flags, STEP_NODE_CYCLE_SET, false);
    }

    const uint16_t setId = allocateCycleSet(*graph, length);
    if (setId == kInvalidId) {
        return {.ok = false, .limitReached = true};
    }

    const auto* created = graph->cycleSet(setId);
    if (created == nullptr) {
        return {.ok = false, .limitReached = true};
    }
    for (uint8_t i = 0; i < StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET; ++i) {
        (void)initializeNodePitchPolicy(
            graph->stepNodes[static_cast<uint16_t>(created->firstStateNode + i)],
            pattern.pitchEditMode
        );
    }

    parent.cycleSetId = setId;
    parent.flags = static_cast<uint16_t>(parent.flags | STEP_NODE_CYCLE_SET);
    pattern.bumpGraphRevision();
    return {.ok = true, .limitReached = false, .id = setId};
}

}  // namespace core::state::sequencer

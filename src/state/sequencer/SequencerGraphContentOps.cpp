#include "state/sequencer/SequencerGraphOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOpsInternal.hpp"

namespace core::state::sequencer {

using namespace graph_ops_internal;

namespace {

FLASHMEM void resetChildNode(StepSequencerStepNode& node) noexcept {
    node = {};
    node.flags = STEP_NODE_ENABLED_OVERRIDE;
}

/**
 * Re-indexs one fixed-capacity child segment for an old-length modulo change.
 * The move order is memmove-safe and retains only one Step node at a time in
 * compiler temporaries; no MAX_STEPS scratch is placed on the stack.
 */
FLASHMEM bool extendLogicalChildSegment(
    StepSequencerGraph& graph,
    uint16_t firstNode,
    uint8_t oldLength,
    int8_t offset,
    uint8_t newLength,
    uint8_t reservedCapacity
) noexcept {
    if (oldLength == 0U || newLength <= oldLength ||
        newLength > reservedCapacity || firstNode == kInvalidId ||
        static_cast<uint32_t>(firstNode) + newLength > graph.stepNodeCount ||
        static_cast<uint32_t>(firstNode) + newLength > graph.stepNodes.size() ||
        offset < -static_cast<int>(oldLength - 1U) ||
        offset > static_cast<int>(oldLength - 1U)) {
        return false;
    }

    auto nodeAt = [&](uint8_t index) -> StepSequencerStepNode& {
        return graph.stepNodes[static_cast<uint16_t>(firstNode + index)];
    };

    if (offset > 0) {
        const uint8_t shifted = static_cast<uint8_t>(offset);
        // Destination is above source and may overlap: copy backwards.
        for (uint8_t remaining = shifted; remaining > 0U; --remaining) {
            const uint8_t local = static_cast<uint8_t>(remaining - 1U);
            const uint8_t source = static_cast<uint8_t>(
                oldLength - shifted + local);
            const uint8_t destination = static_cast<uint8_t>(
                newLength - shifted + local);
            nodeAt(destination) = nodeAt(source);
        }
        for (uint8_t index = static_cast<uint8_t>(oldLength - shifted);
             index < static_cast<uint8_t>(newLength - shifted);
             ++index) {
            resetChildNode(nodeAt(index));
        }
    } else if (offset < 0) {
        const uint8_t shifted = static_cast<uint8_t>(-offset);
        const uint8_t growth = static_cast<uint8_t>(newLength - oldLength);
        if (shifted <= growth) {
            for (uint8_t index = 0U; index < shifted; ++index) {
                nodeAt(static_cast<uint8_t>(oldLength + index)) =
                    nodeAt(index);
            }
            for (uint8_t index = 0U; index < shifted; ++index) {
                resetChildNode(nodeAt(index));
            }
            for (uint8_t index = static_cast<uint8_t>(oldLength + shifted);
                 index < newLength;
                 ++index) {
                resetChildNode(nodeAt(index));
            }
        } else {
            for (uint8_t index = 0U; index < growth; ++index) {
                nodeAt(static_cast<uint8_t>(oldLength + index)) =
                    nodeAt(index);
            }
            // Destination is below source and may overlap: copy forwards.
            for (uint8_t source = growth; source < shifted; ++source) {
                nodeAt(static_cast<uint8_t>(source - growth)) =
                    nodeAt(source);
            }
            for (uint8_t index = static_cast<uint8_t>(shifted - growth);
                 index < shifted;
                 ++index) {
                resetChildNode(nodeAt(index));
            }
        }
    } else {
        for (uint8_t index = oldLength; index < newLength; ++index) {
            resetChildNode(nodeAt(index));
        }
    }
    return true;
}

}  // namespace

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
    if (graph == nullptr ||
        !resizeMicroSequenceUnversioned(*graph, sequenceId, length)) {
        return false;
    }
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
    if (graph == nullptr ||
        !resizeCycleStateSetUnversioned(*graph, cycleSetId, length)) {
        return false;
    }
    pattern.bumpGraphRevision();
    return true;
}

FLASHMEM bool resizeMicroSequenceUnversioned(
    StepSequencerGraph& graph,
    SequencerGraphSequenceId sequenceId,
    uint8_t length
) noexcept {
    if (!graph.enabled || length == 0U ||
        length > StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP) {
        return false;
    }

    auto* sequence = mutableMicroSequence(graph, sequenceId);
    if (sequence == nullptr ||
        length > sequenceReservedCapacity(graph, sequenceId) ||
        sequence->length == length) {
        return false;
    }

    sequence->length = length;
    return true;
}

FLASHMEM bool resizeCycleStateSetUnversioned(
    StepSequencerGraph& graph,
    SequencerGraphCycleSetId cycleSetId,
    uint8_t length
) noexcept {
    if (!graph.enabled || length == 0U ||
        length > StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET) {
        return false;
    }

    auto* cycleSet = mutableCycleSet(graph, cycleSetId);
    if (cycleSet == nullptr ||
        length > cycleSetReservedCapacity(graph, cycleSetId) ||
        cycleSet->length == length) {
        return false;
    }

    cycleSet->length = length;
    return true;
}

FLASHMEM bool extendMicroSequencePreservingLogicalContentUnversioned(
    StepSequencerGraph& graph,
    SequencerGraphSequenceId sequenceId,
    uint8_t length
) noexcept {
    if (!graph.enabled) return false;
    auto* sequence = mutableMicroSequence(graph, sequenceId);
    if (sequence == nullptr ||
        !extendLogicalChildSegment(
            graph,
            sequence->firstStepNode,
            sequence->length,
            sequence->offset,
            length,
            sequenceReservedCapacity(graph, sequenceId))) {
        return false;
    }
    sequence->length = length;
    return true;
}

FLASHMEM bool extendCycleStateSetPreservingLogicalContentUnversioned(
    StepSequencerGraph& graph,
    SequencerGraphCycleSetId cycleSetId,
    uint8_t length
) noexcept {
    if (!graph.enabled) return false;
    auto* cycleSet = mutableCycleSet(graph, cycleSetId);
    if (cycleSet == nullptr ||
        !extendLogicalChildSegment(
            graph,
            cycleSet->firstStateNode,
            cycleSet->length,
            cycleSet->offset,
            length,
            cycleSetReservedCapacity(graph, cycleSetId))) {
        return false;
    }
    cycleSet->length = length;
    return true;
}

FLASHMEM uint8_t sequencerMicroSequenceReservedCapacity(
    const StepSequencerGraph& graph,
    SequencerGraphSequenceId sequenceId
) noexcept {
    if (!graph.enabled) return 0U;
    return sequenceReservedCapacity(graph, sequenceId);
}

FLASHMEM uint8_t sequencerCycleStateSetReservedCapacity(
    const StepSequencerGraph& graph,
    SequencerGraphCycleSetId cycleSetId
) noexcept {
    if (!graph.enabled) return 0U;
    return cycleSetReservedCapacity(graph, cycleSetId);
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

    parent.cycleSetId = setId;
    parent.flags = static_cast<uint16_t>(parent.flags | STEP_NODE_CYCLE_SET);
    pattern.bumpGraphRevision();
    return {.ok = true, .limitReached = false, .id = setId};
}

}  // namespace core::state::sequencer

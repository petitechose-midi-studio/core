#include "state/sequencer/SequencerGraphOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOpsInternal.hpp"

namespace core::state::sequencer {

using namespace graph_ops_internal;

namespace {

FLASHMEM bool sameLocalVariation(
    const oc::note::sequencer::StepSequencerVariationRanges& lhs,
    const oc::note::sequencer::StepSequencerVariationRanges& rhs
) {
    return lhs.pitchSemitones == rhs.pitchSemitones &&
           lhs.velocity == rhs.velocity &&
           lhs.gatePercent == rhs.gatePercent &&
           lhs.nudge == rhs.nudge;
}

FLASHMEM bool sameStepNodePayload(
    const StepSequencerStepNode& lhs,
    const StepSequencerStepNode& rhs
) {
    return lhs.flags == rhs.flags &&
           lhs.childSequenceId == rhs.childSequenceId &&
           lhs.cycleSetId == rhs.cycleSetId &&
           lhs.noteOffset == rhs.noteOffset &&
           lhs.velocityOffset == rhs.velocityOffset &&
           lhs.gateOffset == rhs.gateOffset &&
           lhs.nudgeOffset == rhs.nudgeOffset &&
           lhs.probabilityOffset == rhs.probabilityOffset &&
           sameLocalVariation(lhs.localVariation, rhs.localVariation);
}

FLASHMEM bool hasValidChildren(
    const StepSequencerGraph& graph,
    const StepSequencerStepNode& node
) {
    return (node.has(STEP_NODE_CHILD_SEQUENCE) &&
            graph.sequence(node.childSequenceId) != nullptr) ||
           (node.has(STEP_NODE_CYCLE_SET) &&
            graph.cycleSet(node.cycleSetId) != nullptr);
}

FLASHMEM bool graphHasBudgetFor(
    const StepSequencerGraph& target,
    const GraphCopyBudget& budget
) {
    return budget.valid &&
           static_cast<uint32_t>(target.stepNodeCount) + budget.stepNodes <=
               target.stepNodes.size() &&
           static_cast<uint32_t>(target.sequenceCount) + budget.sequences <=
               target.sequences.size() &&
           static_cast<uint32_t>(target.cycleSetCount) + budget.cycleSets <=
               target.cycleSets.size();
}

}  // namespace

FLASHMEM bool clearNodeChildren(SequencerPatternState& pattern, SequencerGraphNodeId nodeId) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (node.childSequenceId != kInvalidId) {
        node.childSequenceId = kInvalidId;
        changed = true;
    }
    if (node.cycleSetId != kInvalidId) {
        node.cycleSetId = kInvalidId;
        changed = true;
    }
    changed = assignFlag(node.flags, STEP_NODE_CHILD_SEQUENCE, false) || changed;
    changed = assignFlag(node.flags, STEP_NODE_CYCLE_SET, false) || changed;

    bump(pattern, changed);
    return changed;
}

FLASHMEM bool clearNodeChildSequence(SequencerPatternState& pattern,
                                     SequencerGraphNodeId nodeId) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (node.childSequenceId != kInvalidId) {
        node.childSequenceId = kInvalidId;
        changed = true;
    }
    changed = assignFlag(node.flags, STEP_NODE_CHILD_SEQUENCE, false) || changed;

    bump(pattern, changed);
    return changed;
}

FLASHMEM bool clearNodeCycleStateSet(SequencerPatternState& pattern,
                                     SequencerGraphNodeId nodeId) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (node.cycleSetId != kInvalidId) {
        node.cycleSetId = kInvalidId;
        changed = true;
    }
    changed = assignFlag(node.flags, STEP_NODE_CYCLE_SET, false) || changed;

    bump(pattern, changed);
    return changed;
}

FLASHMEM bool resetStepNodePayload(
    SequencerPatternState& pattern,
    SequencerGraphNodeId nodeId,
    SequencerGraphNodeResetMode mode
) {
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !graph->enabled || !hasStepNode(*graph, nodeId)) {
        return false;
    }

    StepSequencerStepNode reset{};
    if (mode == SequencerGraphNodeResetMode::DISABLED_OVERRIDE) {
        reset.flags = STEP_NODE_ENABLED_OVERRIDE;
    }

    auto& node = graph->stepNodes[nodeId];
    if (sameStepNodePayload(node, reset)) return false;

    node = reset;
    pattern.bumpGraphRevision();
    return true;
}

FLASHMEM bool copyStepNodePayloadFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
) {
    if (!ensureGraphRoot(targetPattern)) return false;
    auto* targetGraph = mutableGraph(targetPattern);
    if (targetGraph == nullptr ||
        !hasStepNode(*targetGraph, targetNodeId) ||
        !hasStepNode(sourceGraph, sourceNodeId)) {
        return false;
    }

    const auto& sourceNode = sourceGraph.stepNodes[sourceNodeId];
    if (hasValidChildren(sourceGraph, sourceNode)) {
        const GraphCopyBudget budget = childCopyBudget(sourceGraph, sourceNode);
        if (!graphHasBudgetFor(*targetGraph, budget)) return false;
    }

    auto& targetNode = targetGraph->stepNodes[targetNodeId];
    copyStepNodeValuesWithoutChildren(targetNode, sourceNode);
    if (!copyChildrenIntoNode(*targetGraph, targetNode, sourceGraph, sourceNode)) {
        return false;
    }

    targetPattern.bumpGraphRevision();
    return true;
}

FLASHMEM bool copyNodeChildrenFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
) {
    if (!ensureGraphRoot(targetPattern)) return false;
    auto* targetGraph = mutableGraph(targetPattern);
    if (targetGraph == nullptr ||
        !hasStepNode(*targetGraph, targetNodeId) ||
        !hasStepNode(sourceGraph, sourceNodeId)) {
        return false;
    }

    const auto& sourceNode = sourceGraph.stepNodes[sourceNodeId];
    if (!hasValidChildren(sourceGraph, sourceNode)) return false;

    const GraphCopyBudget budget = childCopyBudget(sourceGraph, sourceNode);
    if (!graphHasBudgetFor(*targetGraph, budget)) return false;

    auto& targetNode = targetGraph->stepNodes[targetNodeId];
    targetNode.childSequenceId = kInvalidId;
    targetNode.cycleSetId = kInvalidId;
    targetNode.flags = static_cast<uint16_t>(
        targetNode.flags & ~(STEP_NODE_CHILD_SEQUENCE | STEP_NODE_CYCLE_SET)
    );

    if (!copyChildrenIntoNode(*targetGraph, targetNode, sourceGraph, sourceNode)) {
        return false;
    }

    targetPattern.bumpGraphRevision();
    return true;
}

FLASHMEM bool copyNodeChildSequenceFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
) {
    if (!ensureGraphRoot(targetPattern)) return false;
    auto* targetGraph = mutableGraph(targetPattern);
    if (targetGraph == nullptr ||
        !hasStepNode(*targetGraph, targetNodeId) ||
        !hasStepNode(sourceGraph, sourceNodeId)) {
        return false;
    }

    const auto& sourceNode = sourceGraph.stepNodes[sourceNodeId];
    if (!sourceNode.has(STEP_NODE_CHILD_SEQUENCE) ||
        sourceGraph.sequence(sourceNode.childSequenceId) == nullptr) {
        return false;
    }

    const GraphCopyBudget budget = sequenceCopyBudget(sourceGraph, sourceNode.childSequenceId);
    if (!budget.valid) return false;
    if (static_cast<uint32_t>(targetGraph->stepNodeCount) + budget.stepNodes >
            targetGraph->stepNodes.size() ||
        static_cast<uint32_t>(targetGraph->sequenceCount) + budget.sequences >
            targetGraph->sequences.size() ||
        static_cast<uint32_t>(targetGraph->cycleSetCount) + budget.cycleSets >
            targetGraph->cycleSets.size()) {
        return false;
    }

    auto& targetNode = targetGraph->stepNodes[targetNodeId];
    targetNode.childSequenceId = kInvalidId;
    targetNode.flags = static_cast<uint16_t>(targetNode.flags & ~STEP_NODE_CHILD_SEQUENCE);

    if (!copySequenceIntoNode(*targetGraph, targetNode, sourceGraph, sourceNode.childSequenceId)) {
        return false;
    }

    targetPattern.bumpGraphRevision();
    return true;
}

FLASHMEM bool copyNodeCycleStateSetFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
) {
    if (!ensureGraphRoot(targetPattern)) return false;
    auto* targetGraph = mutableGraph(targetPattern);
    if (targetGraph == nullptr ||
        !hasStepNode(*targetGraph, targetNodeId) ||
        !hasStepNode(sourceGraph, sourceNodeId)) {
        return false;
    }

    const auto& sourceNode = sourceGraph.stepNodes[sourceNodeId];
    if (!sourceNode.has(STEP_NODE_CYCLE_SET) ||
        sourceGraph.cycleSet(sourceNode.cycleSetId) == nullptr) {
        return false;
    }

    const GraphCopyBudget budget = cycleSetCopyBudget(sourceGraph, sourceNode.cycleSetId);
    if (!budget.valid) return false;
    if (static_cast<uint32_t>(targetGraph->stepNodeCount) + budget.stepNodes >
            targetGraph->stepNodes.size() ||
        static_cast<uint32_t>(targetGraph->sequenceCount) + budget.sequences >
            targetGraph->sequences.size() ||
        static_cast<uint32_t>(targetGraph->cycleSetCount) + budget.cycleSets >
            targetGraph->cycleSets.size()) {
        return false;
    }

    auto& targetNode = targetGraph->stepNodes[targetNodeId];
    targetNode.cycleSetId = kInvalidId;
    targetNode.flags = static_cast<uint16_t>(targetNode.flags & ~STEP_NODE_CYCLE_SET);

    if (!copyCycleSetIntoNode(*targetGraph, targetNode, sourceGraph, sourceNode.cycleSetId)) {
        return false;
    }

    targetPattern.bumpGraphRevision();
    return true;
}

}  // namespace core::state::sequencer

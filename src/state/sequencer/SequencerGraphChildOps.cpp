#include "state/sequencer/SequencerGraphOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOpsInternal.hpp"

namespace core::state::sequencer {

using namespace graph_ops_internal;

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
    const bool hasSourceChildren =
        (sourceNode.has(STEP_NODE_CHILD_SEQUENCE) &&
         sourceGraph.sequence(sourceNode.childSequenceId) != nullptr) ||
        (sourceNode.has(STEP_NODE_CYCLE_SET) &&
         sourceGraph.cycleSet(sourceNode.cycleSetId) != nullptr);
    if (!hasSourceChildren) return false;

    const GraphCopyBudget budget = childCopyBudget(sourceGraph, sourceNode);
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

#include "state/sequencer/SequencerGraphOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOpsInternal.hpp"

namespace core::state::sequencer {

using namespace graph_ops_internal;

namespace {

FLASHMEM bool sameStoredStepNodePayload(
    const StepSequencerStepNode& lhs,
    const StepSequencerStepNode& rhs
) {
    return sameSequencerGraphNodePayload(lhs, rhs) &&
           lhs.childSequenceId == rhs.childSequenceId &&
           lhs.cycleSetId == rhs.cycleSetId;
}

FLASHMEM bool budgetFitsFreshPatternGraph(
    const SequencerGraphCopyBudget& budget
) {
    return budget.stepNodes <=
               StepSequencerGraphLimits::MAX_STEP_NODES -
                   SequencerPatternState::MAX_STEPS &&
           budget.sequences <= StepSequencerGraphLimits::MAX_SEQUENCES - 1U &&
           budget.cycleSets <= StepSequencerGraphLimits::MAX_CYCLE_SETS;
}

FLASHMEM bool validGraphCopyTargetBeforeEnsure(
    const SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    const SequencerGraphCopyBudget& budget
) noexcept {
    // A source can be either a complete Pattern graph or a compact asset graph.
    // Its reachable payload has already been validated by the caller's
    // inspection; only a self-copy must be rejected here.
    if (targetPattern.graph.get() == &sourceGraph) {
        return false;
    }

    const auto* targetGraph = targetPattern.graph.get();
    if (targetGraph == nullptr) {
        return targetNodeId < SequencerPatternState::MAX_STEPS &&
               budgetFitsFreshPatternGraph(budget);
    }
    if (!targetGraph->enabled) {
        return isCanonicalDisabledSequencerGraph(*targetGraph) &&
               targetNodeId < SequencerPatternState::MAX_STEPS &&
               budgetFitsFreshPatternGraph(budget);
    }
    return validInitializedSequencerGraph(*targetGraph) &&
           hasStepNode(*targetGraph, targetNodeId) &&
           sequencerGraphHasCopyCapacity(*targetGraph, budget);
}

FLASHMEM void publishGraphCopyRevision(
    SequencerPatternState& targetPattern,
    bool targetWasInitialized
) noexcept {
    // ensureGraphRoot() publishes the disabled/null -> initialized transition.
    // An already initialized target still needs the payload-copy publication.
    if (targetWasInitialized) targetPattern.bumpGraphRevision();
}

FLASHMEM bool copyStepNodePayloadPrevalidated(
    StepSequencerGraph& targetGraph,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId,
    const SequencerGraphCopyBudget& budget
) {
    if (&targetGraph == &sourceGraph ||
        !hasStepNode(targetGraph, targetNodeId) ||
        !hasStepNode(sourceGraph, sourceNodeId) ||
        !sequencerGraphHasCopyCapacity(targetGraph, budget)) {
        return false;
    }

    auto& targetNode = targetGraph.stepNodes[targetNodeId];
    const auto& sourceNode = sourceGraph.stepNodes[sourceNodeId];
    copyStepNodeValuesWithoutChildren(targetNode, sourceNode);
    return copyChildrenIntoNode(targetGraph, targetNode, sourceGraph, sourceNode);
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
    if (graph == nullptr || !resetStepNodePayloadUnversioned(*graph, nodeId, mode)) return false;
    pattern.bumpGraphRevision();
    return true;
}

FLASHMEM bool resetStepNodePayloadUnversioned(
    StepSequencerGraph& graph,
    SequencerGraphNodeId nodeId,
    SequencerGraphNodeResetMode mode
) noexcept {
    if (!graph.enabled || !hasStepNode(graph, nodeId)) return false;

    StepSequencerStepNode reset{};
    if (mode == SequencerGraphNodeResetMode::DISABLED_OVERRIDE) {
        reset.flags = STEP_NODE_ENABLED_OVERRIDE;
    }

    auto& node = graph.stepNodes[nodeId];
    if (sameStoredStepNodePayload(node, reset)) return false;
    node = reset;
    return true;
}

FLASHMEM bool resetStepNodePayloadPreservingChildrenUnversioned(
    StepSequencerGraph& graph,
    SequencerGraphNodeId nodeId,
    SequencerGraphNodeResetMode mode
) noexcept {
    if (!graph.enabled || !hasStepNode(graph, nodeId)) return false;

    auto& node = graph.stepNodes[nodeId];
    StepSequencerStepNode reset{};
    if (mode == SequencerGraphNodeResetMode::DISABLED_OVERRIDE) {
        reset.flags = STEP_NODE_ENABLED_OVERRIDE;
    }
    reset.flags = static_cast<uint16_t>(
        reset.flags | (node.flags & (STEP_NODE_CHILD_SEQUENCE | STEP_NODE_CYCLE_SET))
    );
    reset.childSequenceId = node.childSequenceId;
    reset.cycleSetId = node.cycleSetId;

    if (sameStoredStepNodePayload(node, reset)) return false;
    node = reset;
    return true;
}

FLASHMEM bool copyStepNodePayloadFromGraphUnversioned(
    StepSequencerGraph& targetGraph,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId,
    uint8_t targetDepth
) noexcept {
    if (&targetGraph == &sourceGraph) return false;
    const auto inspection =
        inspectSequencerGraphPayload(sourceGraph, sourceNodeId, targetDepth);
    if (!inspection.ok()) return false;
    return copyStepNodePayloadPrevalidated(
        targetGraph,
        targetNodeId,
        sourceGraph,
        sourceNodeId,
        inspection.budget
    );
}

FLASHMEM bool copyStepNodePayloadFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
) {
    const auto inspection =
        inspectSequencerGraphPayload(sourceGraph, sourceNodeId, 0U);
    if (!inspection.ok() ||
        !validGraphCopyTargetBeforeEnsure(
            targetPattern,
            targetNodeId,
            sourceGraph,
            inspection.budget)) {
        return false;
    }
    const bool targetWasInitialized = graphView(targetPattern) != nullptr;
    if (!ensureGraphRoot(targetPattern)) return false;
    auto* targetGraph = mutableGraph(targetPattern);
    if (targetGraph == nullptr ||
        !copyStepNodePayloadPrevalidated(
            *targetGraph,
            targetNodeId,
            sourceGraph,
            sourceNodeId,
            inspection.budget)) {
        return false;
    }

    publishGraphCopyRevision(targetPattern, targetWasInitialized);
    return true;
}

FLASHMEM bool copyNodeChildrenFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
) {
    const auto inspection = inspectGraphChildrenForCopy(sourceGraph, sourceNodeId, 0U);
    if (!inspection.ok() || !inspection.payloadPresent ||
        !validGraphCopyTargetBeforeEnsure(
            targetPattern,
            targetNodeId,
            sourceGraph,
            inspection.budget)) {
        return false;
    }
    const bool targetWasInitialized = graphView(targetPattern) != nullptr;
    if (!ensureGraphRoot(targetPattern)) return false;
    auto* targetGraph = mutableGraph(targetPattern);
    if (targetGraph == nullptr ||
        !hasStepNode(*targetGraph, targetNodeId) ||
        !hasStepNode(sourceGraph, sourceNodeId)) {
        return false;
    }

    const auto& sourceNode = sourceGraph.stepNodes[sourceNodeId];
    if (!sequencerGraphHasCopyCapacity(*targetGraph, inspection.budget)) return false;

    auto& targetNode = targetGraph->stepNodes[targetNodeId];
    targetNode.childSequenceId = kInvalidId;
    targetNode.cycleSetId = kInvalidId;
    targetNode.flags = static_cast<uint16_t>(
        targetNode.flags & ~(STEP_NODE_CHILD_SEQUENCE | STEP_NODE_CYCLE_SET)
    );

    if (!copyChildrenIntoNode(*targetGraph, targetNode, sourceGraph, sourceNode)) {
        return false;
    }

    publishGraphCopyRevision(targetPattern, targetWasInitialized);
    return true;
}

FLASHMEM bool copyNodeChildSequenceFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
) {
    if (!sourceGraph.enabled || !hasStepNode(sourceGraph, sourceNodeId)) return false;
    const auto& sourceNode = sourceGraph.stepNodes[sourceNodeId];
    if (!sourceNode.has(STEP_NODE_CHILD_SEQUENCE)) return false;
    const auto inspection = inspectGraphSequenceForCopy(
        sourceGraph, sourceNode.childSequenceId, 0U);
    if (!inspection.ok() ||
        !validGraphCopyTargetBeforeEnsure(
            targetPattern,
            targetNodeId,
            sourceGraph,
            inspection.budget)) {
        return false;
    }
    const bool targetWasInitialized = graphView(targetPattern) != nullptr;
    if (!ensureGraphRoot(targetPattern)) return false;
    auto* targetGraph = mutableGraph(targetPattern);
    if (targetGraph == nullptr ||
        !hasStepNode(*targetGraph, targetNodeId) ||
        !hasStepNode(sourceGraph, sourceNodeId)) {
        return false;
    }

    if (!sequencerGraphHasCopyCapacity(*targetGraph, inspection.budget)) return false;

    auto& targetNode = targetGraph->stepNodes[targetNodeId];
    targetNode.childSequenceId = kInvalidId;
    targetNode.flags = static_cast<uint16_t>(targetNode.flags & ~STEP_NODE_CHILD_SEQUENCE);

    if (!copySequenceIntoNode(*targetGraph, targetNode, sourceGraph, sourceNode.childSequenceId)) {
        return false;
    }

    publishGraphCopyRevision(targetPattern, targetWasInitialized);
    return true;
}

FLASHMEM bool copyNodeCycleStateSetFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
) {
    if (!sourceGraph.enabled || !hasStepNode(sourceGraph, sourceNodeId)) return false;
    const auto& sourceNode = sourceGraph.stepNodes[sourceNodeId];
    if (!sourceNode.has(STEP_NODE_CYCLE_SET)) return false;
    const auto inspection = inspectGraphCycleSetForCopy(
        sourceGraph, sourceNode.cycleSetId, 0U);
    if (!inspection.ok() ||
        !validGraphCopyTargetBeforeEnsure(
            targetPattern,
            targetNodeId,
            sourceGraph,
            inspection.budget)) {
        return false;
    }
    const bool targetWasInitialized = graphView(targetPattern) != nullptr;
    if (!ensureGraphRoot(targetPattern)) return false;
    auto* targetGraph = mutableGraph(targetPattern);
    if (targetGraph == nullptr ||
        !hasStepNode(*targetGraph, targetNodeId) ||
        !hasStepNode(sourceGraph, sourceNodeId)) {
        return false;
    }

    if (!sequencerGraphHasCopyCapacity(*targetGraph, inspection.budget)) return false;

    auto& targetNode = targetGraph->stepNodes[targetNodeId];
    targetNode.cycleSetId = kInvalidId;
    targetNode.flags = static_cast<uint16_t>(targetNode.flags & ~STEP_NODE_CYCLE_SET);

    if (!copyCycleSetIntoNode(*targetGraph, targetNode, sourceGraph, sourceNode.cycleSetId)) {
        return false;
    }

    publishGraphCopyRevision(targetPattern, targetWasInitialized);
    return true;
}

}  // namespace core::state::sequencer

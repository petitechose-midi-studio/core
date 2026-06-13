#include "state/sequencer/SequencerGraphOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOpsInternal.hpp"

namespace core::state::sequencer {

using namespace graph_ops_internal;

FLASHMEM bool setNodeEnabledOverride(SequencerPatternState& pattern,
                                     SequencerGraphNodeId nodeId,
                                     bool enabled) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = assignFlag(node.flags, STEP_NODE_ENABLED_OVERRIDE, true);
    changed = assignFlag(node.flags, STEP_NODE_ENABLED_VALUE, enabled) || changed;
    bump(pattern, changed);
    return changed;
}

FLASHMEM bool clearNodeEnabledOverride(SequencerPatternState& pattern,
                                       SequencerGraphNodeId nodeId) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = assignFlag(node.flags, STEP_NODE_ENABLED_OVERRIDE, false);
    changed = assignFlag(node.flags, STEP_NODE_ENABLED_VALUE, false) || changed;
    bump(pattern, changed);
    return changed;
}

FLASHMEM bool setNodeNoteOffset(SequencerPatternState& pattern,
                                SequencerGraphNodeId nodeId,
                                int8_t offset) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (offset == 0) {
        changed = assignFlag(node.flags, STEP_NODE_NOTE_OFFSET, false);
    } else {
        if (node.noteOffset != offset) {
            node.noteOffset = offset;
            changed = true;
        }
        changed = assignFlag(node.flags, STEP_NODE_NOTE_OFFSET, true) || changed;
    }

    bump(pattern, changed);
    return changed;
}

FLASHMEM bool setNodeVelocityOffset(SequencerPatternState& pattern,
                                    SequencerGraphNodeId nodeId,
                                    int16_t offset) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;
    return setSignedOffset(
        pattern,
        nodeId,
        STEP_NODE_VELOCITY_OFFSET,
        graph->stepNodes[nodeId].velocityOffset,
        offset
    );
}

FLASHMEM bool setNodeGateOffset(SequencerPatternState& pattern,
                                SequencerGraphNodeId nodeId,
                                int16_t offset) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;
    return setSignedOffset(
        pattern,
        nodeId,
        STEP_NODE_GATE_OFFSET,
        graph->stepNodes[nodeId].gateOffset,
        offset
    );
}

FLASHMEM bool setNodeNudgeOffset(SequencerPatternState& pattern,
                                 SequencerGraphNodeId nodeId,
                                 int8_t offset) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (offset == 0) {
        changed = assignFlag(node.flags, STEP_NODE_NUDGE_OFFSET, false);
    } else {
        if (node.nudgeOffset != offset) {
            node.nudgeOffset = offset;
            changed = true;
        }
        changed = assignFlag(node.flags, STEP_NODE_NUDGE_OFFSET, true) || changed;
    }

    bump(pattern, changed);
    return changed;
}

FLASHMEM bool setNodeProbabilityOffset(SequencerPatternState& pattern,
                                       SequencerGraphNodeId nodeId,
                                       int16_t offset) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;
    return setSignedOffset(
        pattern,
        nodeId,
        STEP_NODE_PROBABILITY_OFFSET,
        graph->stepNodes[nodeId].probabilityOffset,
        offset
    );
}

}  // namespace core::state::sequencer

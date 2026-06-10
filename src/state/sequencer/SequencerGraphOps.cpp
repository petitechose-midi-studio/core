#include "state/sequencer/SequencerGraphOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::STEP_NODE_ENABLED_OVERRIDE;
using oc::note::sequencer::STEP_NODE_ENABLED_VALUE;
using oc::note::sequencer::STEP_NODE_GATE_OFFSET;
using oc::note::sequencer::STEP_NODE_NOTE_OFFSET;
using oc::note::sequencer::STEP_NODE_NUDGE_OFFSET;
using oc::note::sequencer::STEP_NODE_PROBABILITY_OFFSET;
using oc::note::sequencer::STEP_NODE_VELOCITY_OFFSET;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerStepNode;

constexpr uint16_t kInvalidId = StepSequencerGraphLimits::INVALID_ID;

FLASHMEM bool hasStepNode(const StepSequencerGraph& graph, uint16_t nodeId) {
    return nodeId < graph.stepNodeCount && nodeId < graph.stepNodes.size();
}

FLASHMEM StepSequencerGraph* mutableGraph(SequencerPatternState& pattern) {
    return pattern.graph.get();
}

FLASHMEM bool ensureGraphAllocated(SequencerPatternState& pattern) {
    if (pattern.graph) return true;
    pattern.graph = core::app::makeExtmemUnique<StepSequencerGraph>();
    return static_cast<bool>(pattern.graph);
}

FLASHMEM bool assignFlag(uint16_t& flags, uint16_t flag, bool enabled) {
    const uint16_t next = enabled
                              ? static_cast<uint16_t>(flags | flag)
                              : static_cast<uint16_t>(flags & ~flag);
    if (next == flags) return false;
    flags = next;
    return true;
}

FLASHMEM uint16_t allocateStepNodes(StepSequencerGraph& graph, uint8_t count) {
    if (count == 0) return kInvalidId;
    const uint32_t nextCount = static_cast<uint32_t>(graph.stepNodeCount) + count;
    if (nextCount > graph.stepNodes.size()) return kInvalidId;

    const uint16_t first = graph.stepNodeCount;
    for (uint16_t i = first; i < nextCount; ++i) {
        graph.stepNodes[i] = StepSequencerStepNode{};
    }
    graph.stepNodeCount = static_cast<uint16_t>(nextCount);
    return first;
}

FLASHMEM uint16_t allocateSequence(StepSequencerGraph& graph,
                                   StepSequencerSequenceKind kind,
                                   uint8_t length,
                                   uint8_t reservedStepNodes) {
    if (length == 0 ||
        reservedStepNodes < length ||
        graph.sequenceCount >= graph.sequences.size()) {
        return kInvalidId;
    }

    const uint16_t firstNode = allocateStepNodes(graph, reservedStepNodes);
    if (firstNode == kInvalidId) return kInvalidId;

    const uint8_t id = graph.sequenceCount++;
    graph.sequences[id] = StepSequencerSequence{
        .kind = kind,
        .firstStepNode = firstNode,
        .length = length,
        .offset = 0,
    };
    return id;
}

FLASHMEM uint8_t sequenceReservedCapacity(const StepSequencerGraph& graph, uint16_t sequenceId) {
    const auto* sequence = graph.sequence(sequenceId);
    if (sequence == nullptr) return 0;

    const uint16_t first = sequence->firstStepNode;
    uint16_t next = graph.stepNodeCount;
    for (uint16_t i = 0; i < graph.sequenceCount && i < graph.sequences.size(); ++i) {
        if (i == sequenceId) continue;
        const auto* candidate = graph.sequence(i);
        if (candidate == nullptr) continue;
        if (candidate->firstStepNode > first) {
            next = std::min<uint16_t>(next, candidate->firstStepNode);
        }
    }
    for (uint16_t i = 0; i < graph.cycleSetCount && i < graph.cycleSets.size(); ++i) {
        const auto* candidate = graph.cycleSet(i);
        if (candidate == nullptr) continue;
        if (candidate->firstStateNode > first) {
            next = std::min<uint16_t>(next, candidate->firstStateNode);
        }
    }

    return next > first ? static_cast<uint8_t>(std::min<uint16_t>(next - first, 255U)) : 0;
}

FLASHMEM uint16_t allocateCycleSet(StepSequencerGraph& graph, uint8_t length) {
    if (length == 0 ||
        length > StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET ||
        graph.cycleSetCount >= graph.cycleSets.size()) {
        return kInvalidId;
    }

    const uint16_t firstNode = allocateStepNodes(graph, length);
    if (firstNode == kInvalidId) return kInvalidId;

    const uint8_t id = graph.cycleSetCount++;
    graph.cycleSets[id] = StepSequencerCycleStateSet{
        .firstStateNode = firstNode,
        .length = length,
    };
    return id;
}

FLASHMEM void bump(SequencerPatternState& pattern, bool changed) {
    if (changed) {
        pattern.bumpGraphRevision();
    }
}

FLASHMEM bool setSignedOffset(SequencerPatternState& pattern,
                              uint16_t nodeId,
                              uint16_t flag,
                              int16_t& target,
                              int16_t value) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (value == 0) {
        changed = assignFlag(node.flags, flag, false);
    } else {
        if (target != value) {
            target = value;
            changed = true;
        }
        changed = assignFlag(node.flags, flag, true) || changed;
    }

    bump(pattern, changed);
    return changed;
}

}  // namespace

FLASHMEM bool ensureGraphRoot(SequencerPatternState& pattern) {
    if (!ensureGraphAllocated(pattern)) return false;

    auto& graph = *pattern.graph;
    const bool validRoot =
        graph.enabled &&
        graph.rootSequenceId == 0 &&
        graph.sequenceCount >= 1 &&
        graph.stepNodeCount >= SequencerPatternState::MAX_STEPS &&
        graph.sequences[0].firstStepNode == 0 &&
        graph.sequences[0].length == SequencerPatternState::MAX_STEPS;
    if (validRoot) {
        return true;
    }

    graph.reset();
    graph.enabled = true;
    graph.rootSequenceId = 0;
    graph.sequenceCount = 1;
    graph.stepNodeCount = SequencerPatternState::MAX_STEPS;
    graph.sequences[0] = StepSequencerSequence{
        .kind = StepSequencerSequenceKind::RootPattern,
        .firstStepNode = 0,
        .length = SequencerPatternState::MAX_STEPS,
        .offset = 0,
    };
    pattern.bumpGraphRevision();
    return true;
}

FLASHMEM void clearGraph(SequencerPatternState& pattern) {
    if (!pattern.graph) {
        return;
    }

    pattern.graph.reset();
    pattern.bumpGraphRevision();
}

FLASHMEM void copyGraph(SequencerPatternState& target, const SequencerPatternState& source) {
    if (!source.graph || !source.graph->enabled) {
        target.graph.reset();
        target.graphRevision.set(source.graphRevision.get());
        return;
    }

    if (!ensureGraphAllocated(target)) return;
    *target.graph = *source.graph;
    target.graphRevision.set(source.graphRevision.get());
}

FLASHMEM const StepSequencerGraph* graphView(const SequencerPatternState& pattern) {
    if (!pattern.graph || !pattern.graph->enabled) return nullptr;
    return pattern.graph.get();
}

FLASHMEM SequencerGraphNodeId rootStepNodeId(uint8_t step) {
    return (step < SequencerPatternState::MAX_STEPS) ? step : kInvalidId;
}

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
    if (graph == nullptr) return false;
    auto* sequence =
        sequenceId < graph->sequenceCount && sequenceId < graph->sequences.size()
            ? &graph->sequences[sequenceId]
            : nullptr;
    if (sequence == nullptr || sequence->kind != StepSequencerSequenceKind::MicroSequence) {
        return false;
    }
    if (length > sequenceReservedCapacity(*graph, sequenceId)) return false;
    if (sequence->length == length) return false;

    sequence->length = length;
    pattern.bumpGraphRevision();
    return true;
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

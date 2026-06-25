#include "state/sequencer/SequencerGraphOps.hpp"

#include "state/sequencer/SequencerGraphOpsInternal.hpp"

#include <algorithm>
#include <array>
#include <utility>

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

FLASHMEM void SequencerGraphCompactionRemap::reset() {
    stepNodes.fill(Limits::INVALID_ID);
    sequences.fill(Limits::INVALID_ID);
    cycleSets.fill(Limits::INVALID_ID);
}

FLASHMEM uint16_t SequencerGraphCompactionRemap::stepNode(uint16_t id) const {
    return id < stepNodes.size() ? stepNodes[id] : Limits::INVALID_ID;
}

FLASHMEM uint16_t SequencerGraphCompactionRemap::sequence(uint16_t id) const {
    return id < sequences.size() ? sequences[id] : Limits::INVALID_ID;
}

FLASHMEM uint16_t SequencerGraphCompactionRemap::cycleSet(uint16_t id) const {
    return id < cycleSets.size() ? cycleSets[id] : Limits::INVALID_ID;
}

namespace graph_ops_internal {

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

FLASHMEM bool hasStepNode(const StepSequencerGraph& graph, uint16_t nodeId) {
    return nodeId < graph.stepNodeCount && nodeId < graph.stepNodes.size();
}

FLASHMEM StepSequencerGraph* mutableGraph(SequencerPatternState& pattern) {
    return pattern.graph.get();
}

FLASHMEM StepSequencerSequence* mutableMicroSequence(
    StepSequencerGraph& graph,
    uint16_t sequenceId
) {
    if (sequenceId >= graph.sequenceCount || sequenceId >= graph.sequences.size()) {
        return nullptr;
    }

    auto& sequence = graph.sequences[sequenceId];
    return sequence.kind == StepSequencerSequenceKind::MicroSequence ? &sequence : nullptr;
}

FLASHMEM StepSequencerCycleStateSet* mutableCycleSet(
    StepSequencerGraph& graph,
    uint16_t cycleSetId
) {
    if (cycleSetId >= graph.cycleSetCount || cycleSetId >= graph.cycleSets.size()) {
        return nullptr;
    }

    return &graph.cycleSets[cycleSetId];
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

FLASHMEM uint8_t cycleSetReservedCapacity(const StepSequencerGraph& graph, uint16_t cycleSetId) {
    const auto* set = graph.cycleSet(cycleSetId);
    if (set == nullptr) return 0;

    const uint16_t first = set->firstStateNode;
    uint16_t next = graph.stepNodeCount;
    for (uint16_t i = 0; i < graph.sequenceCount && i < graph.sequences.size(); ++i) {
        const auto* candidate = graph.sequence(i);
        if (candidate == nullptr) continue;
        if (candidate->firstStepNode > first) {
            next = std::min<uint16_t>(next, candidate->firstStepNode);
        }
    }
    for (uint16_t i = 0; i < graph.cycleSetCount && i < graph.cycleSets.size(); ++i) {
        if (i == cycleSetId) continue;
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

    const uint16_t firstNode =
        allocateStepNodes(graph, StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET);
    if (firstNode == kInvalidId) return kInvalidId;

    const uint8_t id = graph.cycleSetCount++;
    graph.cycleSets[id] = StepSequencerCycleStateSet{
        .firstStateNode = firstNode,
        .length = length,
        .offset = 0,
    };
    return id;
}

FLASHMEM bool appendBudget(GraphCopyBudget& target, const GraphCopyBudget& source) {
    if (!source.valid) {
        target.valid = false;
        return false;
    }
    target.stepNodes = static_cast<uint16_t>(target.stepNodes + source.stepNodes);
    target.sequences = static_cast<uint8_t>(target.sequences + source.sequences);
    target.cycleSets = static_cast<uint8_t>(target.cycleSets + source.cycleSets);
    return true;
}

FLASHMEM GraphCopyBudget childCopyBudget(const StepSequencerGraph& source,
                                         const StepSequencerStepNode& node);

FLASHMEM GraphCopyBudget sequenceCopyBudget(const StepSequencerGraph& source,
                                            uint16_t sequenceId) {
    const auto* sequence = source.sequence(sequenceId);
    if (sequence == nullptr || sequence->kind != StepSequencerSequenceKind::MicroSequence) {
        return {.valid = false};
    }

    GraphCopyBudget budget{
        .stepNodes = StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP,
        .sequences = 1,
        .cycleSets = 0,
        .valid = true,
    };
    for (uint8_t i = 0; i < sequence->length; ++i) {
        const auto* child = source.stepNode(static_cast<uint16_t>(sequence->firstStepNode + i));
        if (child == nullptr) return {.valid = false};
        appendBudget(budget, childCopyBudget(source, *child));
    }
    return budget;
}

FLASHMEM GraphCopyBudget cycleSetCopyBudget(const StepSequencerGraph& source,
                                            uint16_t cycleSetId) {
    const auto* set = source.cycleSet(cycleSetId);
    if (set == nullptr) return {.valid = false};

    GraphCopyBudget budget{
        .stepNodes = StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET,
        .sequences = 0,
        .cycleSets = 1,
        .valid = true,
    };
    for (uint8_t i = 0; i < set->length; ++i) {
        const auto* child = source.stepNode(static_cast<uint16_t>(set->firstStateNode + i));
        if (child == nullptr) return {.valid = false};
        appendBudget(budget, childCopyBudget(source, *child));
    }
    return budget;
}

FLASHMEM GraphCopyBudget childCopyBudget(const StepSequencerGraph& source,
                                         const StepSequencerStepNode& node) {
    GraphCopyBudget budget{};
    if (node.has(STEP_NODE_CHILD_SEQUENCE)) {
        appendBudget(budget, sequenceCopyBudget(source, node.childSequenceId));
    }
    if (node.has(STEP_NODE_CYCLE_SET)) {
        appendBudget(budget, cycleSetCopyBudget(source, node.cycleSetId));
    }
    return budget;
}

FLASHMEM void copyStepNodeValuesWithoutChildren(StepSequencerStepNode& target,
                                                const StepSequencerStepNode& source) {
    target = source;
    target.flags = static_cast<uint16_t>(
        target.flags & ~(STEP_NODE_CHILD_SEQUENCE | STEP_NODE_CYCLE_SET)
    );
    target.childSequenceId = kInvalidId;
    target.cycleSetId = kInvalidId;
}

FLASHMEM bool copyChildrenIntoNode(StepSequencerGraph& target,
                                   StepSequencerStepNode& targetNode,
                                   const StepSequencerGraph& source,
                                   const StepSequencerStepNode& sourceNode);

FLASHMEM bool copySequenceIntoNode(StepSequencerGraph& target,
                                   StepSequencerStepNode& targetNode,
                                   const StepSequencerGraph& source,
                                   uint16_t sourceSequenceId) {
    const auto* sourceSequence = source.sequence(sourceSequenceId);
    if (sourceSequence == nullptr ||
        sourceSequence->kind != StepSequencerSequenceKind::MicroSequence) {
        return false;
    }

    const uint16_t targetSequenceId = allocateSequence(
        target,
        StepSequencerSequenceKind::MicroSequence,
        sourceSequence->length,
        StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP
    );
    if (targetSequenceId == kInvalidId) return false;

    auto& targetSequence = target.sequences[targetSequenceId];
    targetSequence.offset = sourceSequence->offset;
    for (uint8_t i = 0; i < sourceSequence->length; ++i) {
        const auto* sourceChild =
            source.stepNode(static_cast<uint16_t>(sourceSequence->firstStepNode + i));
        if (sourceChild == nullptr) return false;

        auto& targetChild =
            target.stepNodes[static_cast<uint16_t>(targetSequence.firstStepNode + i)];
        copyStepNodeValuesWithoutChildren(targetChild, *sourceChild);
        if (!copyChildrenIntoNode(target, targetChild, source, *sourceChild)) return false;
    }

    targetNode.childSequenceId = targetSequenceId;
    targetNode.flags = static_cast<uint16_t>(targetNode.flags | STEP_NODE_CHILD_SEQUENCE);
    return true;
}

FLASHMEM bool copyCycleSetIntoNode(StepSequencerGraph& target,
                                   StepSequencerStepNode& targetNode,
                                   const StepSequencerGraph& source,
                                   uint16_t sourceCycleSetId) {
    const auto* sourceSet = source.cycleSet(sourceCycleSetId);
    if (sourceSet == nullptr) return false;

    const uint16_t targetCycleSetId = allocateCycleSet(target, sourceSet->length);
    if (targetCycleSetId == kInvalidId) return false;

    auto& targetSet = target.cycleSets[targetCycleSetId];
    targetSet.offset = sourceSet->offset;
    for (uint8_t i = 0; i < sourceSet->length; ++i) {
        const auto* sourceChild =
            source.stepNode(static_cast<uint16_t>(sourceSet->firstStateNode + i));
        if (sourceChild == nullptr) return false;

        auto& targetChild =
            target.stepNodes[static_cast<uint16_t>(targetSet.firstStateNode + i)];
        copyStepNodeValuesWithoutChildren(targetChild, *sourceChild);
        if (!copyChildrenIntoNode(target, targetChild, source, *sourceChild)) return false;
    }

    targetNode.cycleSetId = targetCycleSetId;
    targetNode.flags = static_cast<uint16_t>(targetNode.flags | STEP_NODE_CYCLE_SET);
    return true;
}

FLASHMEM bool copyChildrenIntoNode(StepSequencerGraph& target,
                                   StepSequencerStepNode& targetNode,
                                   const StepSequencerGraph& source,
                                   const StepSequencerStepNode& sourceNode) {
    if (sourceNode.has(STEP_NODE_CHILD_SEQUENCE) &&
        !copySequenceIntoNode(target, targetNode, source, sourceNode.childSequenceId)) {
        return false;
    }
    if (sourceNode.has(STEP_NODE_CYCLE_SET) &&
        !copyCycleSetIntoNode(target, targetNode, source, sourceNode.cycleSetId)) {
        return false;
    }
    return true;
}

FLASHMEM void bump(SequencerPatternState& pattern, bool changed) {
    if (changed) {
        pattern.bumpGraphRevision();
    }
}

struct GraphCompactor {
    const StepSequencerGraph& source;
    StepSequencerGraph& target;
    SequencerGraphCompactionRemap& remap;

    bool copyNode(uint16_t sourceNodeId, uint16_t targetNodeId) {
        if (!hasStepNode(source, sourceNodeId) || !hasStepNode(target, targetNodeId)) {
            return false;
        }

        remap.stepNodes[sourceNodeId] = targetNodeId;
        auto& targetNode = target.stepNodes[targetNodeId];
        const auto& sourceNode = source.stepNodes[sourceNodeId];
        copyStepNodeValuesWithoutChildren(targetNode, sourceNode);

        if (sourceNode.has(STEP_NODE_CHILD_SEQUENCE)) {
            const uint16_t targetSequenceId = copySequence(sourceNode.childSequenceId);
            if (targetSequenceId == kInvalidId) return false;
            targetNode.childSequenceId = targetSequenceId;
            targetNode.flags = static_cast<uint16_t>(targetNode.flags | STEP_NODE_CHILD_SEQUENCE);
        }

        if (sourceNode.has(STEP_NODE_CYCLE_SET)) {
            const uint16_t targetCycleSetId = copyCycleSet(sourceNode.cycleSetId);
            if (targetCycleSetId == kInvalidId) return false;
            targetNode.cycleSetId = targetCycleSetId;
            targetNode.flags = static_cast<uint16_t>(targetNode.flags | STEP_NODE_CYCLE_SET);
        }

        return true;
    }

    uint16_t copySequence(uint16_t sourceSequenceId) {
        const auto* sourceSequence = source.sequence(sourceSequenceId);
        if (sourceSequence == nullptr ||
            sourceSequence->kind != StepSequencerSequenceKind::MicroSequence) {
            return kInvalidId;
        }

        const uint16_t targetSequenceId = allocateSequence(
            target,
            StepSequencerSequenceKind::MicroSequence,
            sourceSequence->length,
            StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP
        );
        if (targetSequenceId == kInvalidId) return kInvalidId;

        remap.sequences[sourceSequenceId] = targetSequenceId;
        auto& targetSequence = target.sequences[targetSequenceId];
        targetSequence.offset = sourceSequence->offset;

        for (uint8_t i = 0; i < sourceSequence->length; ++i) {
            const uint16_t sourceNodeId =
                static_cast<uint16_t>(sourceSequence->firstStepNode + i);
            const uint16_t targetNodeId =
                static_cast<uint16_t>(targetSequence.firstStepNode + i);
            if (!copyNode(sourceNodeId, targetNodeId)) return kInvalidId;
        }

        return targetSequenceId;
    }

    uint16_t copyCycleSet(uint16_t sourceCycleSetId) {
        const auto* sourceSet = source.cycleSet(sourceCycleSetId);
        if (sourceSet == nullptr) return kInvalidId;

        const uint16_t targetCycleSetId = allocateCycleSet(target, sourceSet->length);
        if (targetCycleSetId == kInvalidId) return kInvalidId;

        remap.cycleSets[sourceCycleSetId] = targetCycleSetId;
        auto& targetSet = target.cycleSets[targetCycleSetId];
        targetSet.offset = sourceSet->offset;

        for (uint8_t i = 0; i < sourceSet->length; ++i) {
            const uint16_t sourceNodeId =
                static_cast<uint16_t>(sourceSet->firstStateNode + i);
            const uint16_t targetNodeId =
                static_cast<uint16_t>(targetSet.firstStateNode + i);
            if (!copyNode(sourceNodeId, targetNodeId)) return kInvalidId;
        }

        return targetCycleSetId;
    }

    bool copyRoot() {
        const auto* root = source.sequence(source.rootSequenceId);
        if (root == nullptr ||
            root->kind != StepSequencerSequenceKind::RootPattern ||
            root->firstStepNode != 0 ||
            root->length > SequencerPatternState::MAX_STEPS) {
            return false;
        }

        target.enabled = true;
        target.rootSequenceId = 0;
        target.sequenceCount = 1;
        target.stepNodeCount = SequencerPatternState::MAX_STEPS;
        target.sequences[0] = StepSequencerSequence{
            .kind = StepSequencerSequenceKind::RootPattern,
            .firstStepNode = 0,
            .length = SequencerPatternState::MAX_STEPS,
            .offset = root->offset,
        };
        remap.sequences[source.rootSequenceId] = 0;

        for (uint16_t i = 0; i < SequencerPatternState::MAX_STEPS; ++i) {
            if (!copyNode(i, i)) return false;
        }
        return true;
    }
};

FLASHMEM bool remapChanged(const StepSequencerGraph& source,
                           const StepSequencerGraph& target,
                           const SequencerGraphCompactionRemap& remap) {
    if (source.stepNodeCount != target.stepNodeCount ||
        source.sequenceCount != target.sequenceCount ||
        source.cycleSetCount != target.cycleSetCount) {
        return true;
    }

    for (uint16_t i = 0; i < source.stepNodeCount && i < source.stepNodes.size(); ++i) {
        const uint16_t mapped = remap.stepNode(i);
        if (mapped != kInvalidId && mapped != i) return true;
    }
    for (uint16_t i = 0; i < source.sequenceCount && i < source.sequences.size(); ++i) {
        const uint16_t mapped = remap.sequence(i);
        if (mapped != kInvalidId && mapped != i) return true;
    }
    for (uint16_t i = 0; i < source.cycleSetCount && i < source.cycleSets.size(); ++i) {
        const uint16_t mapped = remap.cycleSet(i);
        if (mapped != kInvalidId && mapped != i) return true;
    }

    return false;
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

FLASHMEM int normalizedRotationOffset(int offsetSteps, uint8_t length) {
    if (length == 0) return 0;
    int normalized = offsetSteps % static_cast<int>(length);
    if (normalized < 0) {
        normalized += length;
    }
    return normalized;
}

FLASHMEM int8_t normalizeContentOffset(int offset, uint8_t length) {
    if (length == 0) return 0;
    const int len = static_cast<int>(length);
    int normalized = offset % len;
    const int maxNegative = -static_cast<int>(length - 1U);
    if (normalized < maxNegative) normalized += len;
    if (normalized > static_cast<int>(length - 1U)) normalized -= len;
    return static_cast<int8_t>(normalized);
}

FLASHMEM uint8_t sourceIndexForLogicalOffset(uint8_t playIndex, int8_t offset, uint8_t length) {
    if (length == 0) return 0;
    int value = static_cast<int>(playIndex) - static_cast<int>(offset);
    const int len = static_cast<int>(length);
    value %= len;
    if (value < 0) value += len;
    return static_cast<uint8_t>(value);
}

FLASHMEM bool rotateStepNodeSegment(
    StepSequencerGraph& graph,
    uint16_t firstNode,
    uint8_t length,
    int offsetSteps,
    int8_t existingLogicalOffset
) {
    if (length == 0 ||
        firstNode == kInvalidId ||
        firstNode >= graph.stepNodeCount ||
        static_cast<uint32_t>(firstNode) + length > graph.stepNodeCount) {
        return false;
    }

    const int normalizedOffset = normalizedRotationOffset(offsetSteps, length);
    const int8_t normalizedLogicalOffset =
        normalizeContentOffset(existingLogicalOffset, length);
    if (normalizedOffset == 0 && normalizedLogicalOffset == 0) return false;

    std::array<StepSequencerStepNode, SequencerPatternState::MAX_STEPS> rotated{};
    static_assert(
        SequencerPatternState::MAX_STEPS >=
            StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP &&
            SequencerPatternState::MAX_STEPS >=
                StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET,
        "step-content rotation scratch must fit every child content kind"
    );

    for (uint8_t i = 0; i < length; ++i) {
        const uint8_t source =
            sourceIndexForLogicalOffset(i, normalizedLogicalOffset, length);
        const uint8_t destination =
            static_cast<uint8_t>((static_cast<int>(i) + normalizedOffset) % length);
        rotated[destination] = graph.stepNodes[static_cast<uint16_t>(firstNode + source)];
    }

    for (uint8_t i = 0; i < length; ++i) {
        graph.stepNodes[static_cast<uint16_t>(firstNode + i)] = rotated[i];
    }
    return true;
}

}  // namespace graph_ops_internal

using namespace graph_ops_internal;

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

FLASHMEM void copyGraph(SequencerPatternState& target,
                         const StepSequencerGraph* source,
                         uint32_t revision) {
    if (source == nullptr || !source->enabled) {
        target.graph.reset();
        target.graphRevision.set(revision);
        return;
    }

    if (!ensureGraphAllocated(target)) return;
    *target.graph = *source;
    target.graphRevision.set(revision);
}

FLASHMEM void copyGraph(SequencerPatternState& target, const SequencerPatternState& source) {
    copyGraph(target, graphView(source), source.graphRevision.get());
}

FLASHMEM const StepSequencerGraph* graphView(const SequencerPatternState& pattern) {
    if (!pattern.graph || !pattern.graph->enabled) return nullptr;
    return pattern.graph.get();
}

FLASHMEM SequencerGraphCompactionResult compactGraph(
    SequencerPatternState& pattern,
    SequencerGraphCompactionRemap* outRemap
) {
    SequencerGraphCompactionRemap localRemap;
    auto& remap = outRemap ? *outRemap : localRemap;
    remap.reset();

    if (!pattern.graph || !pattern.graph->enabled) {
        return {.ok = true, .compacted = false};
    }
    if (!ensureGraphRoot(pattern)) {
        return {.ok = false, .compacted = false};
    }

    const auto* source = pattern.graph.get();
    if (source == nullptr || !source->enabled) {
        return {.ok = true, .compacted = false};
    }

    auto compactedGraph = core::app::makeExtmemUnique<StepSequencerGraph>();
    if (!compactedGraph) {
        return {.ok = false, .compacted = false};
    }

    compactedGraph->reset();
    GraphCompactor compactor{*source, *compactedGraph, remap};
    if (!compactor.copyRoot()) {
        return {.ok = false, .compacted = false};
    }

    const bool compacted = remapChanged(*source, *compactedGraph, remap);
    if (!compacted) {
        return {.ok = true, .compacted = false};
    }

    pattern.graph = std::move(compactedGraph);
    pattern.bumpGraphRevision();
    return {.ok = true, .compacted = true};
}

FLASHMEM SequencerGraphNodeId rootStepNodeId(uint8_t step) {
    return (step < SequencerPatternState::MAX_STEPS) ? step : kInvalidId;
}

FLASHMEM bool stepNodeHasMicroSequence(
    const SequencerPatternState& pattern,
    SequencerGraphNodeId nodeId
) {
    const auto* graph = graphView(pattern);
    const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
    return node != nullptr &&
           node->has(STEP_NODE_CHILD_SEQUENCE) &&
           graph->sequence(node->childSequenceId) != nullptr;
}

FLASHMEM bool stepNodeHasCycleStateSet(
    const SequencerPatternState& pattern,
    SequencerGraphNodeId nodeId
) {
    const auto* graph = graphView(pattern);
    const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
    return node != nullptr &&
           node->has(STEP_NODE_CYCLE_SET) &&
           graph->cycleSet(node->cycleSetId) != nullptr;
}

}  // namespace core::state::sequencer

#include "state/sequencer/SequencerGraphOps.hpp"

#include <algorithm>
#include <array>

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

struct GraphCopyBudget {
    uint16_t stepNodes = 0;
    uint8_t sequences = 0;
    uint8_t cycleSets = 0;
    bool valid = true;
};

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

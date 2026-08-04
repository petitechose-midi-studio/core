#include "state/sequencer/SequencerGraphOps.hpp"

#include <algorithm>
#include <array>
#include <limits>

#include <config/PlatformCompat.hpp>
#include <utility>

#include "state/sequencer/SequencerGraphCanonicalPolicy.hpp"
#include "state/sequencer/SequencerGraphOpsInternal.hpp"

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

namespace {

using oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE;
using oc::note::sequencer::STEP_NODE_CYCLE_SET;
using oc::note::sequencer::StepSequencerChordSpec;
using oc::note::sequencer::StepSequencerCycleStateSet;
using oc::note::sequencer::StepSequencerGraph;
using oc::note::sequencer::StepSequencerGraphLimits;
using oc::note::sequencer::StepSequencerSequence;
using oc::note::sequencer::StepSequencerSequenceKind;
using oc::note::sequencer::StepSequencerStepNode;
using oc::note::sequencer::StepSequencerVariationRanges;

constexpr uint16_t kCyclePathTag = 0x8000U;

enum class GraphInspectionScope : uint8_t {
    FullNode = 0,
    Children,
    Sequence,
    CycleSet,
};

struct GraphInspectionContext {
    const StepSequencerGraph& source;
    SequencerGraphCopyBudget budget{};
    SequencerGraphPayloadInspectionStatus status =
        SequencerGraphPayloadInspectionStatus::Ok;
    std::array<uint16_t, StepSequencerGraphLimits::MAX_DEPTH>
        activeContainers{};
    uint8_t activeContainerCount = 0;
};

static_assert(sizeof(GraphInspectionContext) <= 32U);

FLASHMEM bool sameVariationExact(
    const StepSequencerVariationRanges& lhs,
    const StepSequencerVariationRanges& rhs
) noexcept {
    return lhs.pitchSemitones == rhs.pitchSemitones &&
           lhs.velocity == rhs.velocity &&
           lhs.gatePercent == rhs.gatePercent &&
           lhs.nudge == rhs.nudge;
}

FLASHMEM bool sameChordExact(
    const StepSequencerChordSpec& lhs,
    const StepSequencerChordSpec& rhs
) noexcept {
    return lhs.voiceCount == rhs.voiceCount &&
           lhs.harmonyData == rhs.harmonyData &&
           lhs.voicingData == rhs.voicingData &&
           lhs.inversionData == rhs.inversionData &&
           lhs.strum == rhs.strum &&
           lhs.velocityCurve == rhs.velocityCurve &&
           lhs.customIntervalExtension == rhs.customIntervalExtension;
}

FLASHMEM bool sameStepNodeExact(
    const StepSequencerStepNode& lhs,
    const StepSequencerStepNode& rhs
) noexcept {
    return lhs.flags == rhs.flags &&
           lhs.velocityOffset == rhs.velocityOffset &&
           lhs.gateOffset == rhs.gateOffset &&
           lhs.probabilityOffset == rhs.probabilityOffset &&
           lhs.childSequenceId == rhs.childSequenceId &&
           lhs.cycleSetId == rhs.cycleSetId &&
           sameVariationExact(lhs.localVariation, rhs.localVariation) &&
           sameChordExact(lhs.chordSpec, rhs.chordSpec) &&
           lhs.chordMode == rhs.chordMode &&
           lhs.noteOffset == rhs.noteOffset &&
           lhs.nudgeOffset == rhs.nudgeOffset;
}

FLASHMEM bool sameSequenceExact(
    const StepSequencerSequence& lhs,
    const StepSequencerSequence& rhs
) noexcept {
    return lhs.kind == rhs.kind &&
           lhs.firstStepNode == rhs.firstStepNode &&
           lhs.length == rhs.length &&
           lhs.offset == rhs.offset;
}

FLASHMEM bool sameCycleSetExact(
    const StepSequencerCycleStateSet& lhs,
    const StepSequencerCycleStateSet& rhs
) noexcept {
    return lhs.firstStateNode == rhs.firstStateNode &&
           lhs.length == rhs.length &&
           lhs.offset == rhs.offset;
}

FLASHMEM bool copiedNodePayloadPresent(
    const StepSequencerStepNode& node
) noexcept {
    const StepSequencerStepNode empty{};
    return node.flags != empty.flags ||
           node.velocityOffset != empty.velocityOffset ||
           node.gateOffset != empty.gateOffset ||
           node.probabilityOffset != empty.probabilityOffset ||
           !sameVariationExact(node.localVariation, empty.localVariation) ||
           !sameChordExact(node.chordSpec, empty.chordSpec) ||
           node.chordMode != empty.chordMode ||
           node.noteOffset != empty.noteOffset ||
           node.nudgeOffset != empty.nudgeOffset;
}

FLASHMEM bool sourceGraphShapeValid(
    const StepSequencerGraph& source
) noexcept {
    return source.enabled &&
           source.stepNodeCount <= source.stepNodes.size() &&
           source.sequenceCount <= source.sequences.size() &&
           source.cycleSetCount <= source.cycleSets.size();
}

FLASHMEM bool initializedPatternGraphShapeValid(
    const StepSequencerGraph& graph
) noexcept {
    if (!sourceGraphShapeValid(graph) ||
        graph.rootSequenceId != 0U ||
        graph.sequenceCount < 1U ||
        graph.stepNodeCount < SequencerPatternState::MAX_STEPS) {
        return false;
    }

    const auto& root = graph.sequences[0];
    return root.kind == StepSequencerSequenceKind::RootPattern &&
           root.firstStepNode == 0U &&
           root.length == SequencerPatternState::MAX_STEPS;
}

FLASHMEM bool failInspection(
    GraphInspectionContext& context,
    SequencerGraphPayloadInspectionStatus status
) noexcept {
    context.status = status;
    return false;
}

FLASHMEM bool pushActiveContainer(
    GraphInspectionContext& context,
    uint16_t token
) noexcept {
    for (uint8_t index = 0; index < context.activeContainerCount; ++index) {
        if (context.activeContainers[index] == token) {
            return failInspection(
                context,
                SequencerGraphPayloadInspectionStatus::CycleDetected
            );
        }
    }
    if (context.activeContainerCount >= context.activeContainers.size()) {
        return failInspection(
            context,
            SequencerGraphPayloadInspectionStatus::DepthExceeded
        );
    }
    context.activeContainers[context.activeContainerCount++] = token;
    return true;
}

FLASHMEM void popActiveContainer(GraphInspectionContext& context) noexcept {
    if (context.activeContainerCount > 0U) {
        --context.activeContainerCount;
    }
}

FLASHMEM bool inspectGraphNode(
    GraphInspectionContext& context,
    uint16_t nodeId,
    uint8_t depth
) noexcept;

FLASHMEM bool inspectGraphSequence(
    GraphInspectionContext& context,
    uint16_t sequenceId,
    uint8_t childDepth
) noexcept {
    if (sequenceId >= context.source.sequenceCount ||
        sequenceId >= context.source.sequences.size()) {
        return failInspection(
            context,
            SequencerGraphPayloadInspectionStatus::MalformedGraph
        );
    }

    const auto& sequence = context.source.sequences[sequenceId];
    const uint32_t end = static_cast<uint32_t>(sequence.firstStepNode) +
                         sequence.length;
    if (sequence.kind != StepSequencerSequenceKind::MicroSequence ||
        sequence.length == 0U ||
        sequence.length >
            StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP ||
        sequence.firstStepNode == StepSequencerGraphLimits::INVALID_ID ||
        sequence.firstStepNode >= context.source.stepNodeCount ||
        sequence.firstStepNode >= context.source.stepNodes.size() ||
        end > context.source.stepNodeCount ||
        end > context.source.stepNodes.size()) {
        return failInspection(
            context,
            SequencerGraphPayloadInspectionStatus::MalformedGraph
        );
    }

    if (!pushActiveContainer(context, sequenceId)) return false;
    const SequencerGraphCopyBudget addition{
        .stepNodes =
            StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP,
        .sequences = 1U,
        .cycleSets = 0U,
    };
    if (!appendSequencerGraphCopyBudget(context.budget, addition)) {
        popActiveContainer(context);
        return failInspection(
            context,
            SequencerGraphPayloadInspectionStatus::ArithmeticOverflow
        );
    }

    for (uint8_t index = 0; index < sequence.length; ++index) {
        const auto nodeId = static_cast<uint16_t>(
            sequence.firstStepNode + index
        );
        if (!inspectGraphNode(context, nodeId, childDepth)) {
            popActiveContainer(context);
            return false;
        }
    }
    popActiveContainer(context);
    return true;
}

FLASHMEM bool inspectGraphCycleSet(
    GraphInspectionContext& context,
    uint16_t cycleSetId,
    uint8_t childDepth
) noexcept {
    if (cycleSetId >= context.source.cycleSetCount ||
        cycleSetId >= context.source.cycleSets.size()) {
        return failInspection(
            context,
            SequencerGraphPayloadInspectionStatus::MalformedGraph
        );
    }

    const auto& cycleSet = context.source.cycleSets[cycleSetId];
    const uint32_t end = static_cast<uint32_t>(cycleSet.firstStateNode) +
                         cycleSet.length;
    if (cycleSet.length == 0U ||
        cycleSet.length > StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET ||
        cycleSet.firstStateNode == StepSequencerGraphLimits::INVALID_ID ||
        cycleSet.firstStateNode >= context.source.stepNodeCount ||
        cycleSet.firstStateNode >= context.source.stepNodes.size() ||
        end > context.source.stepNodeCount ||
        end > context.source.stepNodes.size()) {
        return failInspection(
            context,
            SequencerGraphPayloadInspectionStatus::MalformedGraph
        );
    }

    const uint16_t token = static_cast<uint16_t>(kCyclePathTag | cycleSetId);
    if (!pushActiveContainer(context, token)) return false;
    const SequencerGraphCopyBudget addition{
        .stepNodes = StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET,
        .sequences = 0U,
        .cycleSets = 1U,
    };
    if (!appendSequencerGraphCopyBudget(context.budget, addition)) {
        popActiveContainer(context);
        return failInspection(
            context,
            SequencerGraphPayloadInspectionStatus::ArithmeticOverflow
        );
    }

    for (uint8_t index = 0; index < cycleSet.length; ++index) {
        const auto nodeId = static_cast<uint16_t>(
            cycleSet.firstStateNode + index
        );
        if (!inspectGraphNode(context, nodeId, childDepth)) {
            popActiveContainer(context);
            return false;
        }
    }
    popActiveContainer(context);
    return true;
}

FLASHMEM bool inspectGraphNode(
    GraphInspectionContext& context,
    uint16_t nodeId,
    uint8_t depth
) noexcept {
    const auto* node = context.source.stepNode(nodeId);
    if (node == nullptr ||
        !graph_canonical_policy::stepNodeIsCanonical(*node)) {
        return failInspection(
            context,
            SequencerGraphPayloadInspectionStatus::MalformedGraph
        );
    }

    const bool hasSequence = node->has(STEP_NODE_CHILD_SEQUENCE);
    const bool hasCycleSet = node->has(STEP_NODE_CYCLE_SET);
    if ((hasSequence || hasCycleSet) &&
        depth >= StepSequencerGraphLimits::MAX_DEPTH - 1U) {
        return failInspection(
            context,
            SequencerGraphPayloadInspectionStatus::DepthExceeded
        );
    }

    const uint8_t childDepth = static_cast<uint8_t>(depth + 1U);
    if (hasSequence &&
        !inspectGraphSequence(context, node->childSequenceId, childDepth)) {
        return false;
    }
    if (hasCycleSet &&
        !inspectGraphCycleSet(context, node->cycleSetId, childDepth)) {
        return false;
    }
    return true;
}

FLASHMEM SequencerGraphPayloadInspection inspectGraphPayload(
    const StepSequencerGraph& source,
    uint16_t nodeId,
    uint16_t containerId,
    uint8_t targetDepth,
    GraphInspectionScope scope
) noexcept {
    if (!sourceGraphShapeValid(source) ||
        targetDepth >= StepSequencerGraphLimits::MAX_DEPTH) {
        return {
            .status = SequencerGraphPayloadInspectionStatus::InvalidArgument,
        };
    }

    GraphInspectionContext context{.source = source};
    bool valid = false;
    bool payloadPresent = false;
    switch (scope) {
        case GraphInspectionScope::FullNode:
        case GraphInspectionScope::Children: {
            const auto* node = source.stepNode(nodeId);
            if (node == nullptr ||
                !graph_canonical_policy::stepNodeIsCanonical(*node)) {
                return {
                    .status =
                        SequencerGraphPayloadInspectionStatus::MalformedGraph,
                };
            }
            payloadPresent = scope == GraphInspectionScope::FullNode
                ? copiedNodePayloadPresent(*node)
                : node->has(STEP_NODE_CHILD_SEQUENCE) ||
                      node->has(STEP_NODE_CYCLE_SET);
            valid = inspectGraphNode(
                context,
                nodeId,
                targetDepth
            );
            break;
        }
        case GraphInspectionScope::Sequence:
            if (targetDepth >= StepSequencerGraphLimits::MAX_DEPTH - 1U) {
                return {
                    .status =
                        SequencerGraphPayloadInspectionStatus::DepthExceeded,
                };
            }
            payloadPresent = true;
            valid = inspectGraphSequence(
                context,
                containerId,
                static_cast<uint8_t>(targetDepth + 1U)
            );
            break;
        case GraphInspectionScope::CycleSet:
            if (targetDepth >= StepSequencerGraphLimits::MAX_DEPTH - 1U) {
                return {
                    .status =
                        SequencerGraphPayloadInspectionStatus::DepthExceeded,
                };
            }
            payloadPresent = true;
            valid = inspectGraphCycleSet(
                context,
                containerId,
                static_cast<uint8_t>(targetDepth + 1U)
            );
            break;
    }

    if (!valid) {
        return {
            .status = context.status,
        };
    }
    return {
        .budget = context.budget,
        .status = SequencerGraphPayloadInspectionStatus::Ok,
        .payloadPresent = payloadPresent,
    };
}

FLASHMEM bool sameGraphNodePayloadRecursive(
    const StepSequencerGraph& lhs,
    uint16_t lhsNodeId,
    const StepSequencerGraph& rhs,
    uint16_t rhsNodeId,
    uint8_t depth
) noexcept {
    if (depth >= StepSequencerGraphLimits::MAX_DEPTH) return false;

    const auto* lhsNode = lhs.stepNode(lhsNodeId);
    const auto* rhsNode = rhs.stepNode(rhsNodeId);
    if (lhsNode == nullptr || rhsNode == nullptr ||
        !sameSequencerGraphNodePayload(*lhsNode, *rhsNode)) {
        return false;
    }

    if (lhsNode->has(STEP_NODE_CHILD_SEQUENCE)) {
        const auto* lhsSequence = lhs.sequence(lhsNode->childSequenceId);
        const auto* rhsSequence = rhs.sequence(rhsNode->childSequenceId);
        if (lhsSequence == nullptr || rhsSequence == nullptr ||
            lhsSequence->kind != rhsSequence->kind ||
            lhsSequence->length != rhsSequence->length ||
            lhsSequence->offset != rhsSequence->offset) {
            return false;
        }
        for (uint8_t index = 0; index < lhsSequence->length; ++index) {
            if (!sameGraphNodePayloadRecursive(
                    lhs,
                    static_cast<uint16_t>(lhsSequence->firstStepNode + index),
                    rhs,
                    static_cast<uint16_t>(rhsSequence->firstStepNode + index),
                    static_cast<uint8_t>(depth + 1U))) {
                return false;
            }
        }
    }

    if (lhsNode->has(STEP_NODE_CYCLE_SET)) {
        const auto* lhsSet = lhs.cycleSet(lhsNode->cycleSetId);
        const auto* rhsSet = rhs.cycleSet(rhsNode->cycleSetId);
        if (lhsSet == nullptr || rhsSet == nullptr ||
            lhsSet->length != rhsSet->length ||
            lhsSet->offset != rhsSet->offset) {
            return false;
        }
        for (uint8_t index = 0; index < lhsSet->length; ++index) {
            if (!sameGraphNodePayloadRecursive(
                    lhs,
                    static_cast<uint16_t>(lhsSet->firstStateNode + index),
                    rhs,
                    static_cast<uint16_t>(rhsSet->firstStateNode + index),
                    static_cast<uint8_t>(depth + 1U))) {
                return false;
            }
        }
    }

    return true;
}

struct GlobalGraphValidationContext {
    static constexpr std::size_t NODE_WORD_COUNT =
        (StepSequencerGraphLimits::MAX_STEP_NODES + 63U) / 64U;

    const StepSequencerGraph& graph;
    std::array<uint64_t, NODE_WORD_COUNT> ownedNodes{};
    uint32_t seenSequences = 0U;
    uint64_t seenCycleSets = 0U;
};

static_assert(
    sizeof(GlobalGraphValidationContext) <= 96U,
    "global Graph validation must remain a bounded scalar frame"
);

FLASHMEM bool claimGlobalGraphNode(
    GlobalGraphValidationContext& context,
    uint16_t nodeId
) noexcept {
    if (nodeId >= context.graph.stepNodeCount ||
        nodeId >= StepSequencerGraphLimits::MAX_STEP_NODES) {
        return false;
    }
    const std::size_t word = nodeId / 64U;
    const uint64_t bit = uint64_t{1} << (nodeId % 64U);
    if ((context.ownedNodes[word] & bit) != 0U) return false;
    context.ownedNodes[word] |= bit;
    return true;
}

FLASHMEM bool validateGlobalGraphNode(
    GlobalGraphValidationContext& context,
    uint16_t nodeId,
    uint8_t depth
) noexcept;

FLASHMEM bool validateGlobalGraphSequence(
    GlobalGraphValidationContext& context,
    uint16_t sequenceId,
    uint8_t depth
) noexcept {
    if (sequenceId == context.graph.rootSequenceId ||
        sequenceId >= context.graph.sequenceCount ||
        sequenceId >= StepSequencerGraphLimits::MAX_SEQUENCES) {
        return false;
    }
    const uint32_t bit = uint32_t{1} << sequenceId;
    if ((context.seenSequences & bit) != 0U) return false;

    const auto& sequence = context.graph.sequences[sequenceId];
    const uint32_t end = static_cast<uint32_t>(sequence.firstStepNode) +
                         sequence.length;
    const int minimumOffset = -static_cast<int>(sequence.length - 1U);
    const int maximumOffset = static_cast<int>(sequence.length - 1U);
    if (sequence.kind != StepSequencerSequenceKind::MicroSequence ||
        sequence.length == 0U ||
        sequence.length >
            StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP ||
        sequence.firstStepNode == StepSequencerGraphLimits::INVALID_ID ||
        end > context.graph.stepNodeCount ||
        end > context.graph.stepNodes.size() ||
        sequence.offset < minimumOffset || sequence.offset > maximumOffset) {
        return false;
    }

    context.seenSequences |= bit;
    for (uint16_t node = sequence.firstStepNode; node < end; ++node) {
        if (!claimGlobalGraphNode(context, node)) return false;
    }
    for (uint16_t node = sequence.firstStepNode; node < end; ++node) {
        if (!validateGlobalGraphNode(context, node, depth)) return false;
    }
    return true;
}

FLASHMEM bool validateGlobalGraphCycleSet(
    GlobalGraphValidationContext& context,
    uint16_t cycleSetId,
    uint8_t depth
) noexcept {
    if (cycleSetId >= context.graph.cycleSetCount ||
        cycleSetId >= StepSequencerGraphLimits::MAX_CYCLE_SETS) {
        return false;
    }
    const uint64_t bit = uint64_t{1} << cycleSetId;
    if ((context.seenCycleSets & bit) != 0U) return false;

    const auto& cycleSet = context.graph.cycleSets[cycleSetId];
    const uint32_t end = static_cast<uint32_t>(cycleSet.firstStateNode) +
                         cycleSet.length;
    const int minimumOffset = -static_cast<int>(cycleSet.length - 1U);
    const int maximumOffset = static_cast<int>(cycleSet.length - 1U);
    if (cycleSet.length == 0U ||
        cycleSet.length > StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET ||
        cycleSet.firstStateNode == StepSequencerGraphLimits::INVALID_ID ||
        end > context.graph.stepNodeCount ||
        end > context.graph.stepNodes.size() ||
        cycleSet.offset < minimumOffset || cycleSet.offset > maximumOffset) {
        return false;
    }

    context.seenCycleSets |= bit;
    for (uint16_t node = cycleSet.firstStateNode; node < end; ++node) {
        if (!claimGlobalGraphNode(context, node)) return false;
    }
    for (uint16_t node = cycleSet.firstStateNode; node < end; ++node) {
        if (!validateGlobalGraphNode(context, node, depth)) return false;
    }
    return true;
}

FLASHMEM bool validateGlobalGraphNode(
    GlobalGraphValidationContext& context,
    uint16_t nodeId,
    uint8_t depth
) noexcept {
    if (depth >= StepSequencerGraphLimits::MAX_DEPTH) return false;
    const auto* node = context.graph.stepNode(nodeId);
    if (node == nullptr ||
        !graph_canonical_policy::stepNodeIsCanonical(*node)) {
        return false;
    }

    const bool hasSequence = node->has(STEP_NODE_CHILD_SEQUENCE);
    const bool hasCycleSet = node->has(STEP_NODE_CYCLE_SET);
    if ((!hasSequence &&
         node->childSequenceId != StepSequencerGraphLimits::INVALID_ID) ||
        (!hasCycleSet &&
         node->cycleSetId != StepSequencerGraphLimits::INVALID_ID) ||
        ((hasSequence || hasCycleSet) &&
         depth >= StepSequencerGraphLimits::MAX_DEPTH - 1U)) {
        return false;
    }

    const uint8_t childDepth = static_cast<uint8_t>(depth + 1U);
    if (hasSequence &&
        !validateGlobalGraphSequence(
            context, node->childSequenceId, childDepth)) {
        return false;
    }
    if (hasCycleSet &&
        !validateGlobalGraphCycleSet(
            context, node->cycleSetId, childDepth)) {
        return false;
    }
    return true;
}

FLASHMEM bool validateGlobalGraphTopology(
    const StepSequencerGraph& graph
) noexcept {
    if (!initializedPatternGraphShapeValid(graph)) return false;

    const auto& root = graph.sequences[graph.rootSequenceId];
    const int minimumOffset = -static_cast<int>(root.length - 1U);
    const int maximumOffset = static_cast<int>(root.length - 1U);
    if (!graph_canonical_policy::sequenceIsCanonical(root) ||
        root.offset < minimumOffset || root.offset > maximumOffset) {
        return false;
    }

    GlobalGraphValidationContext context{.graph = graph};
    context.seenSequences = uint32_t{1} << graph.rootSequenceId;
    for (uint16_t node = 0U;
         node < SequencerPatternState::MAX_STEPS;
         ++node) {
        if (!claimGlobalGraphNode(context, node)) return false;
    }
    for (uint16_t node = 0U;
         node < SequencerPatternState::MAX_STEPS;
         ++node) {
        if (!validateGlobalGraphNode(context, node, 0U)) return false;
    }
    return true;
}

}  // namespace

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

FLASHMEM StepSequencerSequence* mutableMicroSequence(StepSequencerGraph& graph,
                                                     uint16_t sequenceId) {
    if (sequenceId >= graph.sequenceCount || sequenceId >= graph.sequences.size()) {
        return nullptr;
    }

    auto& sequence = graph.sequences[sequenceId];
    return sequence.kind == StepSequencerSequenceKind::MicroSequence ? &sequence : nullptr;
}

FLASHMEM StepSequencerCycleStateSet* mutableCycleSet(StepSequencerGraph& graph,
                                                     uint16_t cycleSetId) {
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
    const uint16_t next =
        enabled ? static_cast<uint16_t>(flags | flag) : static_cast<uint16_t>(flags & ~flag);
    if (next == flags) return false;
    flags = next;
    return true;
}

FLASHMEM uint16_t allocateStepNodes(StepSequencerGraph& graph, uint8_t count) {
    if (count == 0) return kInvalidId;
    const uint32_t nextCount = static_cast<uint32_t>(graph.stepNodeCount) + count;
    if (nextCount > graph.stepNodes.size()) return kInvalidId;

    const uint16_t first = graph.stepNodeCount;
    for (uint16_t i = first; i < nextCount; ++i) { graph.stepNodes[i] = StepSequencerStepNode{}; }
    graph.stepNodeCount = static_cast<uint16_t>(nextCount);
    return first;
}

FLASHMEM uint16_t allocateSequence(StepSequencerGraph& graph, StepSequencerSequenceKind kind,
                                   uint8_t length, uint8_t reservedStepNodes) {
    if (length == 0 || reservedStepNodes < length ||
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
    if (length == 0 || length > StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET ||
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

FLASHMEM SequencerGraphPayloadInspection inspectGraphChildrenForCopy(
    const StepSequencerGraph& source,
    uint16_t nodeId,
    uint8_t targetDepth
) noexcept {
    return inspectGraphPayload(
        source,
        nodeId,
        kInvalidId,
        targetDepth,
        GraphInspectionScope::Children
    );
}

FLASHMEM SequencerGraphPayloadInspection inspectGraphSequenceForCopy(
    const StepSequencerGraph& source,
    uint16_t sequenceId,
    uint8_t targetDepth
) noexcept {
    return inspectGraphPayload(
        source,
        kInvalidId,
        sequenceId,
        targetDepth,
        GraphInspectionScope::Sequence
    );
}

FLASHMEM SequencerGraphPayloadInspection inspectGraphCycleSetForCopy(
    const StepSequencerGraph& source,
    uint16_t cycleSetId,
    uint8_t targetDepth
) noexcept {
    return inspectGraphPayload(
        source,
        kInvalidId,
        cycleSetId,
        targetDepth,
        GraphInspectionScope::CycleSet
    );
}

FLASHMEM void copyStepNodeValuesWithoutChildren(StepSequencerStepNode& target,
                                                const StepSequencerStepNode& source) {
    target = source;
    target.flags =
        static_cast<uint16_t>(target.flags & ~(STEP_NODE_CHILD_SEQUENCE | STEP_NODE_CYCLE_SET));
    target.childSequenceId = kInvalidId;
    target.cycleSetId = kInvalidId;
}

FLASHMEM bool copyChildrenIntoNode(StepSequencerGraph& target, StepSequencerStepNode& targetNode,
                                   const StepSequencerGraph& source,
                                   const StepSequencerStepNode& sourceNode);

FLASHMEM bool copySequenceIntoNode(StepSequencerGraph& target, StepSequencerStepNode& targetNode,
                                   const StepSequencerGraph& source, uint16_t sourceSequenceId) {
    const auto* sourceSequence = source.sequence(sourceSequenceId);
    if (sourceSequence == nullptr ||
        sourceSequence->kind != StepSequencerSequenceKind::MicroSequence) {
        return false;
    }

    const uint16_t targetSequenceId =
        allocateSequence(target, StepSequencerSequenceKind::MicroSequence, sourceSequence->length,
                         StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);
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

FLASHMEM bool copyCycleSetIntoNode(StepSequencerGraph& target, StepSequencerStepNode& targetNode,
                                   const StepSequencerGraph& source, uint16_t sourceCycleSetId) {
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

        auto& targetChild = target.stepNodes[static_cast<uint16_t>(targetSet.firstStateNode + i)];
        copyStepNodeValuesWithoutChildren(targetChild, *sourceChild);
        if (!copyChildrenIntoNode(target, targetChild, source, *sourceChild)) return false;
    }

    targetNode.cycleSetId = targetCycleSetId;
    targetNode.flags = static_cast<uint16_t>(targetNode.flags | STEP_NODE_CYCLE_SET);
    return true;
}

FLASHMEM bool copyChildrenIntoNode(StepSequencerGraph& target, StepSequencerStepNode& targetNode,
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
    if (changed) { pattern.bumpGraphRevision(); }
}

struct GraphCompactor {
    const StepSequencerGraph& source;
    StepSequencerGraph& target;
    SequencerGraphCompactionRemap& remap;

    bool copyNode(uint16_t sourceNodeId, uint16_t targetNodeId);
    uint16_t copySequence(uint16_t sourceSequenceId);
    uint16_t copyCycleSet(uint16_t sourceCycleSetId);
    bool copyRoot();
};

FLASHMEM bool GraphCompactor::copyNode(uint16_t sourceNodeId, uint16_t targetNodeId) {
    if (!hasStepNode(source, sourceNodeId) || !hasStepNode(target, targetNodeId)) { return false; }

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

FLASHMEM uint16_t GraphCompactor::copySequence(uint16_t sourceSequenceId) {
    const auto* sourceSequence = source.sequence(sourceSequenceId);
    if (sourceSequence == nullptr ||
        sourceSequence->kind != StepSequencerSequenceKind::MicroSequence) {
        return kInvalidId;
    }

    const uint16_t targetSequenceId =
        allocateSequence(target, StepSequencerSequenceKind::MicroSequence, sourceSequence->length,
                         StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP);
    if (targetSequenceId == kInvalidId) return kInvalidId;

    remap.sequences[sourceSequenceId] = targetSequenceId;
    auto& targetSequence = target.sequences[targetSequenceId];
    targetSequence.offset = sourceSequence->offset;

    for (uint8_t i = 0; i < sourceSequence->length; ++i) {
        const uint16_t sourceNodeId = static_cast<uint16_t>(sourceSequence->firstStepNode + i);
        const uint16_t targetNodeId = static_cast<uint16_t>(targetSequence.firstStepNode + i);
        if (!copyNode(sourceNodeId, targetNodeId)) return kInvalidId;
    }

    return targetSequenceId;
}

FLASHMEM uint16_t GraphCompactor::copyCycleSet(uint16_t sourceCycleSetId) {
    const auto* sourceSet = source.cycleSet(sourceCycleSetId);
    if (sourceSet == nullptr) return kInvalidId;

    const uint16_t targetCycleSetId = allocateCycleSet(target, sourceSet->length);
    if (targetCycleSetId == kInvalidId) return kInvalidId;

    remap.cycleSets[sourceCycleSetId] = targetCycleSetId;
    auto& targetSet = target.cycleSets[targetCycleSetId];
    targetSet.offset = sourceSet->offset;

    for (uint8_t i = 0; i < sourceSet->length; ++i) {
        const uint16_t sourceNodeId = static_cast<uint16_t>(sourceSet->firstStateNode + i);
        const uint16_t targetNodeId = static_cast<uint16_t>(targetSet.firstStateNode + i);
        if (!copyNode(sourceNodeId, targetNodeId)) return kInvalidId;
    }

    return targetCycleSetId;
}

FLASHMEM bool GraphCompactor::copyRoot() {
    const auto* root = source.sequence(source.rootSequenceId);
    if (root == nullptr || root->kind != StepSequencerSequenceKind::RootPattern ||
        root->firstStepNode != 0 || root->length > SequencerPatternState::MAX_STEPS) {
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

FLASHMEM bool remapChanged(const StepSequencerGraph& source, const StepSequencerGraph& target,
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

FLASHMEM bool setSignedOffset(SequencerPatternState& pattern, uint16_t nodeId, uint16_t flag,
                              int16_t& target, int16_t value) {
    if (!ensureGraphRoot(pattern)) return false;
    auto* graph = mutableGraph(pattern);
    if (graph == nullptr || !hasStepNode(*graph, nodeId)) return false;

    auto& node = graph->stepNodes[nodeId];
    bool changed = false;
    if (value == 0) {
        changed = assignFlag(node.flags, flag, false);
        if (target != 0) {
            target = 0;
            changed = true;
        }
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
    if (normalized < 0) { normalized += length; }
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

FLASHMEM bool rotateStepNodeSegment(StepSequencerGraph& graph, uint16_t firstNode, uint8_t length,
                                    int offsetSteps, int8_t existingLogicalOffset) {
    if (length == 0 || firstNode == kInvalidId || firstNode >= graph.stepNodeCount ||
        static_cast<uint32_t>(firstNode) + length > graph.stepNodeCount) {
        return false;
    }

    const int normalizedOffset = normalizedRotationOffset(offsetSteps, length);
    const int8_t normalizedLogicalOffset = normalizeContentOffset(existingLogicalOffset, length);
    if (normalizedOffset == 0 && normalizedLogicalOffset == 0) return false;

    std::array<StepSequencerStepNode, SequencerPatternState::MAX_STEPS> rotated{};
    static_assert(
        SequencerPatternState::MAX_STEPS >=
                StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP &&
            SequencerPatternState::MAX_STEPS >= StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET,
        "step-content rotation scratch must fit every child content kind");

    for (uint16_t i = 0; i < length; ++i) {
        const auto stepIndex = static_cast<uint8_t>(i);
        const uint8_t source =
            sourceIndexForLogicalOffset(stepIndex, normalizedLogicalOffset, length);
        const uint8_t destination =
            static_cast<uint8_t>((static_cast<int>(i) + normalizedOffset) % length);
        rotated[destination] = graph.stepNodes[static_cast<uint16_t>(firstNode + source)];
    }

    for (uint16_t i = 0; i < length; ++i) {
        graph.stepNodes[static_cast<uint16_t>(firstNode + i)] = rotated[i];
    }
    return true;
}

}  // namespace graph_ops_internal

using namespace graph_ops_internal;

FLASHMEM bool appendSequencerGraphCopyBudget(
    SequencerGraphCopyBudget& aggregate,
    const SequencerGraphCopyBudget& addition
) noexcept {
    constexpr uint32_t kMax = std::numeric_limits<uint32_t>::max();
    if (addition.stepNodes > kMax - aggregate.stepNodes ||
        addition.sequences > kMax - aggregate.sequences ||
        addition.cycleSets > kMax - aggregate.cycleSets) {
        return false;
    }

    aggregate.stepNodes += addition.stepNodes;
    aggregate.sequences += addition.sequences;
    aggregate.cycleSets += addition.cycleSets;
    return true;
}

FLASHMEM SequencerGraphPayloadInspection inspectSequencerGraphPayload(
    const StepSequencerGraph& source,
    SequencerGraphNodeId sourceNodeId,
    uint8_t targetDepth
) noexcept {
    return inspectGraphPayload(
        source,
        sourceNodeId,
        kInvalidId,
        targetDepth,
        GraphInspectionScope::FullNode
    );
}

FLASHMEM bool validInitializedSequencerGraph(
    const StepSequencerGraph& graph
) noexcept {
    return validateGlobalGraphTopology(graph);
}

FLASHMEM bool sequencerGraphHasCopyCapacity(
    const StepSequencerGraph& target,
    const SequencerGraphCopyBudget& budget
) noexcept {
    if (!initializedPatternGraphShapeValid(target)) return false;

    return budget.stepNodes <= target.stepNodes.size() - target.stepNodeCount &&
           budget.sequences <= target.sequences.size() - target.sequenceCount &&
           budget.cycleSets <= target.cycleSets.size() - target.cycleSetCount;
}

FLASHMEM bool isCanonicalDisabledSequencerGraph(
    const StepSequencerGraph& graph
) noexcept {
    if (graph.enabled ||
        graph.rootSequenceId != StepSequencerGraphLimits::INVALID_ID ||
        graph.stepNodeCount != 0U ||
        graph.sequenceCount != 0U ||
        graph.cycleSetCount != 0U) {
        return false;
    }

    const StepSequencerStepNode emptyNode{};
    for (const auto& node : graph.stepNodes) {
        if (!sameStepNodeExact(node, emptyNode)) return false;
    }

    const StepSequencerSequence emptySequence{};
    for (const auto& sequence : graph.sequences) {
        if (!sameSequenceExact(sequence, emptySequence)) return false;
    }

    const StepSequencerCycleStateSet emptyCycleSet{};
    for (const auto& cycleSet : graph.cycleSets) {
        if (!sameCycleSetExact(cycleSet, emptyCycleSet)) return false;
    }
    return true;
}

FLASHMEM bool initializeSequencerGraphRootUnversioned(
    StepSequencerGraph& graph
) noexcept {
    if (graph.enabled) return validInitializedSequencerGraph(graph);
    if (!isCanonicalDisabledSequencerGraph(graph)) return false;

    graph.enabled = true;
    graph.rootSequenceId = 0U;
    graph.stepNodeCount = SequencerPatternState::MAX_STEPS;
    graph.sequenceCount = 1U;
    graph.cycleSetCount = 0U;
    graph.sequences[0] = StepSequencerSequence{
        .kind = StepSequencerSequenceKind::RootPattern,
        .firstStepNode = 0U,
        .length = SequencerPatternState::MAX_STEPS,
        .offset = 0,
    };
    return true;
}

FLASHMEM bool sameSequencerGraphNodePayload(
    const StepSequencerStepNode& lhs,
    const StepSequencerStepNode& rhs
) noexcept {
    return lhs.flags == rhs.flags &&
           lhs.velocityOffset == rhs.velocityOffset &&
           lhs.gateOffset == rhs.gateOffset &&
           lhs.probabilityOffset == rhs.probabilityOffset &&
           sameVariationExact(lhs.localVariation, rhs.localVariation) &&
           sameChordExact(lhs.chordSpec, rhs.chordSpec) &&
           lhs.chordMode == rhs.chordMode &&
           lhs.noteOffset == rhs.noteOffset &&
           lhs.nudgeOffset == rhs.nudgeOffset;
}

FLASHMEM bool isDefaultSequencerGraphNodePayload(
    const StepSequencerStepNode& node
) noexcept {
    const StepSequencerStepNode empty{};
    return graph_canonical_policy::stepNodeIsCanonical(node) &&
           sameSequencerGraphNodePayload(node, empty);
}

FLASHMEM SequencerGraphPayloadComparison compareSequencerGraphPayloads(
    const StepSequencerGraph& lhs,
    SequencerGraphNodeId lhsNodeId,
    const StepSequencerGraph& rhs,
    SequencerGraphNodeId rhsNodeId,
    uint8_t targetDepth
) noexcept {
    const auto lhsInspection =
        inspectSequencerGraphPayload(lhs, lhsNodeId, targetDepth);
    if (!lhsInspection.ok()) {
        return {.status = lhsInspection.status, .same = false};
    }

    const auto rhsInspection =
        inspectSequencerGraphPayload(rhs, rhsNodeId, targetDepth);
    if (!rhsInspection.ok()) {
        return {.status = rhsInspection.status, .same = false};
    }

    return {
        .status = SequencerGraphPayloadInspectionStatus::Ok,
        .same = sameGraphNodePayloadRecursive(
            lhs, lhsNodeId, rhs, rhsNodeId, targetDepth),
    };
}

FLASHMEM bool ensureGraphRoot(SequencerPatternState& pattern) {
    if (!ensureGraphAllocated(pattern)) return false;

    auto& graph = *pattern.graph;
    if (graph.enabled) return validInitializedSequencerGraph(graph);
    if (!isCanonicalDisabledSequencerGraph(graph)) return false;
    if (!initializeSequencerGraphRootUnversioned(graph)) return false;
    pattern.bumpGraphRevision();
    return true;
}

FLASHMEM void clearGraph(SequencerPatternState& pattern) {
    if (!pattern.graph) { return; }

    pattern.graph.reset();
    pattern.bumpGraphRevision();
}

FLASHMEM bool copyGraph(SequencerPatternState& target, const StepSequencerGraph* source,
                        uint32_t revision) {
    if (source == nullptr || !source->enabled) {
        target.graph.reset();
        target.graphRevision.set(revision);
        return true;
    }

    if (target.graph) {
        *target.graph = *source;
    } else {
        auto graph = core::app::makeExtmemUnique<StepSequencerGraph>(*source);
        if (!graph) return false;
        target.graph = std::move(graph);
    }
    target.graphRevision.set(revision);
    return true;
}

FLASHMEM bool copyGraph(SequencerPatternState& target, const SequencerPatternState& source) {
    return copyGraph(target, graphView(source), source.graphRevision.get());
}

FLASHMEM const StepSequencerGraph* graphView(const SequencerPatternState& pattern) {
    if (!pattern.graph || !pattern.graph->enabled) return nullptr;
    return pattern.graph.get();
}

FLASHMEM SequencerGraphCompactionResult compactGraph(SequencerPatternState& pattern,
                                                     SequencerGraphCompactionRemap& remap) {
    if (!pattern.graph || !pattern.graph->enabled) { return {.ok = true, .compacted = false}; }
    if (!validInitializedSequencerGraph(*pattern.graph)) {
        return {.ok = false, .compacted = false};
    }

    const auto* source = pattern.graph.get();
    if (source == nullptr || !source->enabled) { return {.ok = true, .compacted = false}; }

    auto compactedGraph = core::app::makeExtmemUnique<StepSequencerGraph>();
    if (!compactedGraph) { return {.ok = false, .compacted = false}; }

    return compactGraphUsingReservedStorage(pattern, *compactedGraph, remap);
}

FLASHMEM SequencerGraphCompactionResult compactGraph(SequencerPatternState& pattern) {
    SequencerGraphCompactionRemap remap;
    return compactGraph(pattern, remap);
}

FLASHMEM SequencerGraphCompactionResult
compactGraphUsingReservedStorage(SequencerPatternState& pattern, StepSequencerGraph& reservedGraph,
                                 SequencerGraphCompactionRemap& remap) {
    remap.reset();

    if (!pattern.graph || !pattern.graph->enabled) { return {.ok = true, .compacted = false}; }
    if (!validInitializedSequencerGraph(*pattern.graph)) {
        return {.ok = false, .compacted = false};
    }

    const auto* source = pattern.graph.get();
    if (source == nullptr || !source->enabled) { return {.ok = true, .compacted = false}; }
    if (source == &reservedGraph) { return {.ok = false, .compacted = false}; }

    reservedGraph.reset();
    GraphCompactor compactor{*source, reservedGraph, remap};
    if (!compactor.copyRoot()) { return {.ok = false, .compacted = false}; }

    const bool compacted = remapChanged(*source, reservedGraph, remap);
    if (!compacted) { return {.ok = true, .compacted = false}; }

    *pattern.graph = reservedGraph;
    pattern.bumpGraphRevision();
    return {.ok = true, .compacted = true};
}

FLASHMEM SequencerGraphNodeId rootStepNodeId(uint8_t step) {
    return (step < SequencerPatternState::MAX_STEPS) ? step : kInvalidId;
}

FLASHMEM bool stepNodeHasMicroSequence(const SequencerPatternState& pattern,
                                       SequencerGraphNodeId nodeId) {
    const auto* graph = graphView(pattern);
    const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
    return node != nullptr && node->has(STEP_NODE_CHILD_SEQUENCE) &&
           graph->sequence(node->childSequenceId) != nullptr;
}

FLASHMEM bool stepNodeHasCycleStateSet(const SequencerPatternState& pattern,
                                       SequencerGraphNodeId nodeId) {
    const auto* graph = graphView(pattern);
    const auto* node = graph ? graph->stepNode(nodeId) : nullptr;
    return node != nullptr && node->has(STEP_NODE_CYCLE_SET) &&
           graph->cycleSet(node->cycleSetId) != nullptr;
}

FLASHMEM bool stepNodeHasAnyChildContent(const SequencerPatternState& pattern,
                                         SequencerGraphNodeId nodeId) {
    return stepNodeHasMicroSequence(pattern, nodeId) || stepNodeHasCycleStateSet(pattern, nodeId);
}

}  // namespace core::state::sequencer

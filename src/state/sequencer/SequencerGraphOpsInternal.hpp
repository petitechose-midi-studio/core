#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/sequencer/SequencerPatternState.hpp"

namespace core::state::sequencer::graph_ops_internal {

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
using oc::note::sequencer::StepSequencerStepNodeFlags;

inline constexpr uint16_t kInvalidId = StepSequencerGraphLimits::INVALID_ID;

struct GraphCopyBudget {
    uint16_t stepNodes = 0;
    uint8_t sequences = 0;
    uint8_t cycleSets = 0;
    bool valid = true;
};

FLASHMEM bool hasStepNode(const StepSequencerGraph& graph, uint16_t nodeId);
FLASHMEM StepSequencerGraph* mutableGraph(SequencerPatternState& pattern);
FLASHMEM StepSequencerSequence* mutableMicroSequence(
    StepSequencerGraph& graph,
    uint16_t sequenceId
);
FLASHMEM StepSequencerCycleStateSet* mutableCycleSet(
    StepSequencerGraph& graph,
    uint16_t cycleSetId
);
FLASHMEM bool ensureGraphAllocated(SequencerPatternState& pattern);
FLASHMEM bool assignFlag(uint16_t& flags, uint16_t flag, bool enabled);
FLASHMEM uint16_t allocateStepNodes(StepSequencerGraph& graph, uint8_t count);
FLASHMEM uint16_t allocateSequence(
    StepSequencerGraph& graph,
    StepSequencerSequenceKind kind,
    uint8_t length,
    uint8_t reservedStepNodes
);
FLASHMEM uint8_t sequenceReservedCapacity(const StepSequencerGraph& graph, uint16_t sequenceId);
FLASHMEM uint8_t cycleSetReservedCapacity(const StepSequencerGraph& graph, uint16_t cycleSetId);
FLASHMEM uint16_t allocateCycleSet(StepSequencerGraph& graph, uint8_t length);
FLASHMEM bool appendBudget(GraphCopyBudget& target, const GraphCopyBudget& source);
FLASHMEM GraphCopyBudget childCopyBudget(
    const StepSequencerGraph& source,
    const StepSequencerStepNode& node
);
FLASHMEM GraphCopyBudget sequenceCopyBudget(const StepSequencerGraph& source, uint16_t sequenceId);
FLASHMEM GraphCopyBudget cycleSetCopyBudget(const StepSequencerGraph& source, uint16_t cycleSetId);
FLASHMEM void copyStepNodeValuesWithoutChildren(
    StepSequencerStepNode& target,
    const StepSequencerStepNode& source
);
FLASHMEM bool copyChildrenIntoNode(
    StepSequencerGraph& target,
    StepSequencerStepNode& targetNode,
    const StepSequencerGraph& source,
    const StepSequencerStepNode& sourceNode
);
FLASHMEM bool copySequenceIntoNode(
    StepSequencerGraph& target,
    StepSequencerStepNode& targetNode,
    const StepSequencerGraph& source,
    uint16_t sourceSequenceId
);
FLASHMEM bool copyCycleSetIntoNode(
    StepSequencerGraph& target,
    StepSequencerStepNode& targetNode,
    const StepSequencerGraph& source,
    uint16_t sourceCycleSetId
);
FLASHMEM void bump(SequencerPatternState& pattern, bool changed);
FLASHMEM bool setSignedOffset(
    SequencerPatternState& pattern,
    uint16_t nodeId,
    uint16_t flag,
    int16_t& target,
    int16_t value
);
FLASHMEM int normalizedRotationOffset(int offsetSteps, uint8_t length);
FLASHMEM int8_t normalizeContentOffset(int offset, uint8_t length);
FLASHMEM uint8_t sourceIndexForLogicalOffset(uint8_t playIndex, int8_t offset, uint8_t length);
FLASHMEM bool rotateStepNodeSegment(
    StepSequencerGraph& graph,
    uint16_t firstStepNode,
    uint8_t length,
    int offsetSteps,
    int8_t existingContentOffset
);

}  // namespace core::state::sequencer::graph_ops_internal

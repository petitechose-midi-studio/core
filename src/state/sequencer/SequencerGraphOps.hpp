#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerGraph.hpp>

#include "state/sequencer/SequencerPatternState.hpp"

namespace core::state::sequencer {

using SequencerGraphNodeId = uint16_t;
using SequencerGraphSequenceId = uint16_t;
using SequencerGraphCycleSetId = uint16_t;

struct SequencerGraphCreateResult {
    bool ok = false;
    bool limitReached = false;
    uint16_t id = oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
};

bool ensureGraphRoot(SequencerPatternState& pattern);
void clearGraph(SequencerPatternState& pattern);
void copyGraph(SequencerPatternState& target, const SequencerPatternState& source);

const oc::note::sequencer::StepSequencerGraph* graphView(const SequencerPatternState& pattern);

SequencerGraphNodeId rootStepNodeId(uint8_t step);

SequencerGraphCreateResult createMicroSequence(
    SequencerPatternState& pattern,
    SequencerGraphNodeId parentNodeId,
    uint8_t length
);
bool resizeMicroSequence(
    SequencerPatternState& pattern,
    SequencerGraphSequenceId sequenceId,
    uint8_t length
);
bool resizeCycleStateSet(
    SequencerPatternState& pattern,
    SequencerGraphCycleSetId cycleSetId,
    uint8_t length
);
bool setMicroSequenceOffset(
    SequencerPatternState& pattern,
    SequencerGraphSequenceId sequenceId,
    int8_t offset
);
bool setCycleStateSetOffset(
    SequencerPatternState& pattern,
    SequencerGraphCycleSetId cycleSetId,
    int8_t offset
);
bool rotateRootStepNodes(SequencerPatternState& pattern, int offsetSteps);
bool rotateMicroSequenceSteps(
    SequencerPatternState& pattern,
    SequencerGraphSequenceId sequenceId,
    int offsetSteps
);
bool rotateCycleStateSetSteps(
    SequencerPatternState& pattern,
    SequencerGraphCycleSetId cycleSetId,
    int offsetSteps
);

SequencerGraphCreateResult createCycleStateSet(
    SequencerPatternState& pattern,
    SequencerGraphNodeId parentNodeId,
    uint8_t length
);

bool clearNodeChildren(SequencerPatternState& pattern, SequencerGraphNodeId nodeId);
bool clearNodeChildSequence(SequencerPatternState& pattern, SequencerGraphNodeId nodeId);
bool clearNodeCycleStateSet(SequencerPatternState& pattern, SequencerGraphNodeId nodeId);
bool copyNodeChildrenFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const oc::note::sequencer::StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
);
bool copyNodeChildSequenceFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const oc::note::sequencer::StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
);
bool copyNodeCycleStateSetFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const oc::note::sequencer::StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
);

bool setNodeEnabledOverride(SequencerPatternState& pattern,
                            SequencerGraphNodeId nodeId,
                            bool enabled);
bool clearNodeEnabledOverride(SequencerPatternState& pattern, SequencerGraphNodeId nodeId);
bool setNodeNoteOffset(SequencerPatternState& pattern, SequencerGraphNodeId nodeId, int8_t offset);
bool setNodeVelocityOffset(SequencerPatternState& pattern,
                           SequencerGraphNodeId nodeId,
                           int16_t offset);
bool setNodeGateOffset(SequencerPatternState& pattern, SequencerGraphNodeId nodeId, int16_t offset);
bool setNodeNudgeOffset(SequencerPatternState& pattern, SequencerGraphNodeId nodeId, int8_t offset);
bool setNodeProbabilityOffset(SequencerPatternState& pattern,
                              SequencerGraphNodeId nodeId,
                              int16_t offset);

}  // namespace core::state::sequencer

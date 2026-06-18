#pragma once

#include <array>
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

struct SequencerGraphCompactionRemap {
    using Limits = oc::note::sequencer::StepSequencerGraphLimits;

    std::array<uint16_t, Limits::MAX_STEP_NODES> stepNodes{};
    std::array<uint16_t, Limits::MAX_SEQUENCES> sequences{};
    std::array<uint16_t, Limits::MAX_CYCLE_SETS> cycleSets{};

    void reset();
    uint16_t stepNode(uint16_t id) const;
    uint16_t sequence(uint16_t id) const;
    uint16_t cycleSet(uint16_t id) const;
};

struct SequencerGraphCompactionResult {
    bool ok = false;
    bool compacted = false;
};

bool ensureGraphRoot(SequencerPatternState& pattern);
void clearGraph(SequencerPatternState& pattern);
void copyGraph(SequencerPatternState& target, const SequencerPatternState& source);
void copyGraph(SequencerPatternState& target,
               const oc::note::sequencer::StepSequencerGraph* source,
               uint32_t revision);
SequencerGraphCompactionResult compactGraph(
    SequencerPatternState& pattern,
    SequencerGraphCompactionRemap* remap = nullptr
);

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

enum class SequencerGraphNodeResetMode : uint8_t {
    DEFAULT = 0,
    DISABLED_OVERRIDE,
};

bool resetStepNodePayload(
    SequencerPatternState& pattern,
    SequencerGraphNodeId nodeId,
    SequencerGraphNodeResetMode mode = SequencerGraphNodeResetMode::DEFAULT
);
bool copyStepNodePayloadFromGraph(
    SequencerPatternState& targetPattern,
    SequencerGraphNodeId targetNodeId,
    const oc::note::sequencer::StepSequencerGraph& sourceGraph,
    SequencerGraphNodeId sourceNodeId
);
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
uint8_t nodeLocalVariationRange(const oc::note::sequencer::StepSequencerStepNode& node,
                                StepProperty property);
bool setNodeLocalVariationRange(SequencerPatternState& pattern,
                                SequencerGraphNodeId nodeId,
                                StepProperty property,
                                uint8_t range);

}  // namespace core::state::sequencer

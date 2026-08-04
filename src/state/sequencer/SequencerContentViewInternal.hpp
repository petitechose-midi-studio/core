#pragma once

#include <cstdint>

#include <config/PlatformCompat.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerPitchEditAuthority.hpp"

namespace core::state::sequencer::content_view_internal {

using GraphLimits = oc::note::sequencer::StepSequencerGraphLimits;
using Node = oc::note::sequencer::StepSequencerStepNode;
using Sequence = oc::note::sequencer::StepSequencerSequence;
using CycleSet = oc::note::sequencer::StepSequencerCycleStateSet;

inline constexpr uint8_t MICRO_LENGTH_MIN = 2;
inline constexpr uint8_t MICRO_LENGTH_MAX = GraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP;
inline constexpr uint8_t CYCLE_STATE_LENGTH_MIN = 1;
inline constexpr uint8_t CYCLE_STATE_LENGTH_MAX = GraphLimits::MAX_CYCLE_STATES_PER_SET;
inline constexpr uint16_t kInvalidId = GraphLimits::INVALID_ID;

struct ResolvedStep {
    bool valid = false;
    bool enabled = false;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint16_t gate = 0;
    int8_t nudge = 0;
    uint8_t probability = SequencerState::DEFAULT_PROBABILITY;
    oc::note::sequencer::StepSequencerChordState chordState =
        oc::note::sequencer::defaultRootChordState();
    oc::note::sequencer::StepSequencerInheritedChord inheritedChord{};
};

int targetValueFromNormalized(
    StepProperty property,
    float normalized,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
float valueToNormalized(
    StepProperty property,
    int value,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);

bool nodeEnabled(const Node& node);
ResolvedStep applyNode(
    ResolvedStep parent,
    const Node& node,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    bool noteOffsetsUseScaleDegrees
);
ResolvedStep contentBaseForKind(
    ResolvedStep owner,
    SequencerContentViewKind kind,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
ResolvedStep rootBase(const SequencerState& sequencer, uint8_t rootStep);
const Node* graphNode(const SequencerState& sequencer, SequencerGraphNodeId nodeId);
bool nodeHasMicroSequence(
    const oc::note::sequencer::StepSequencerGraph& graph,
    const Node& node
);
bool nodeHasCycleStates(
    const oc::note::sequencer::StepSequencerGraph& graph,
    const Node& node
);

uint8_t normalizeSequenceIndex(uint8_t playIndex, int8_t offset, uint8_t length);
uint32_t boundaryTick(uint8_t playIndex, uint32_t spanTicks, uint8_t length);
uint32_t effectiveGateSpan(uint32_t spanTicks, uint16_t gatePercent);
bool resolveRepresentativeChildContentStep(
    const oc::note::sequencer::StepSequencerGraph& graph,
    const Node& node,
    ResolvedStep& current,
    uint8_t depth,
    uint32_t localCycleIndex,
    uint8_t microPlayIndex,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    bool noteOffsetsUseScaleDegrees,
    SequencerChildContentSummary* outSummary = nullptr
);

bool ownsSequence(
    const oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphNodeId ownerNodeId,
    SequencerGraphSequenceId sequenceId
);
bool ownsCycleSet(
    const oc::note::sequencer::StepSequencerGraph& graph,
    SequencerGraphNodeId ownerNodeId,
    SequencerGraphCycleSetId cycleSetId
);
void syncPublicViewFields(SequencerContentViewState& view);
bool pushFrame(
    SequencerState& sequencer,
    SequencerContentViewKind kind,
    SequencerGraphNodeId ownerNodeId,
    SequencerGraphSequenceId sequenceId,
    SequencerGraphCycleSetId cycleSetId,
    uint8_t length
);
bool validateFrame(const SequencerState& sequencer, SequencerContentViewFrame& frame);
ResolvedStep resolveOwnerStepAtDepth(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    uint8_t frameDepth,
    bool noteOffsetsUseScaleDegrees
);
ResolvedStep resolveOwnerStep(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    bool noteOffsetsUseScaleDegrees
);
SequencerGraphNodeId stepNodeIdForFrame(
    const SequencerState& sequencer,
    const SequencerContentViewFrame& frame,
    uint8_t step
);

bool setNodeProperty(
    SequencerState& sequencer,
    SequencerGraphNodeId nodeId,
    StepProperty property,
    int baseValue,
    int targetValue,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
int baseValueForProperty(const SequencerContentStepProjection& projection, StepProperty property);
int resolvedValueForProperty(
    const SequencerContentStepProjection& projection,
    StepProperty property
);

}  // namespace core::state::sequencer::content_view_internal

#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerChord.hpp>
#include <oc/note/sequencer/StepSequencerScale.hpp>
#include <oc/note/sequencer/StepSequencerVariation.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state {
struct StructureClipboardState;
}

namespace core::state::sequencer {

static constexpr uint8_t DEFAULT_MICRO_SEQUENCE_LENGTH = 2;
static constexpr uint8_t DEFAULT_CYCLE_STATE_COUNT = 4;

enum class StepContentChildKind : uint8_t {
    MICRO_SEQUENCE = 0,
    CYCLE_STATES,
};

enum class StepContentCreationBlockReason : uint8_t {
    NONE = 0,
    INACTIVE_CONTEXT,
    MAX_DEPTH_REACHED,
    INVALID_FOCUSED_STEP,
    GRAPH_LIMIT_REACHED,
};

struct StepContentCreationAvailability {
    bool canCreateOrOpen = false;
    bool opensExisting = false;
    StepContentCreationBlockReason blockedReason =
        StepContentCreationBlockReason::INACTIVE_CONTEXT;
};

struct StepContentOpenResult {
    bool opened = false;
    bool created = false;
    StepContentCreationBlockReason blockedReason =
        StepContentCreationBlockReason::INACTIVE_CONTEXT;
    StepContentChildKind childKind = StepContentChildKind::MICRO_SEQUENCE;
    SequencerGraphNodeId ownerNodeId =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
    uint16_t contentId =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;
};

bool isRootContentView(const SequencerState& sequencer);
bool isChildContentView(const SequencerState& sequencer);
bool isMicroSequenceContentView(const SequencerState& sequencer);
bool isCycleStatesContentView(const SequencerState& sequencer);
uint8_t activeContentDepth(const SequencerState& sequencer);

struct SequencerContentStepProjection {
    static constexpr uint16_t INVALID_ID =
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID;

    bool valid = false;
    bool rootContext = true;
    uint8_t rootStep = 0;
    uint8_t localStep = 0;
    SequencerGraphNodeId nodeId = INVALID_ID;
    bool enabled = false;
    bool parentEnabled = true;
    uint8_t parentNote = 0;
    uint8_t note = 0;
    uint8_t parentVelocity = 0;
    uint8_t velocity = 0;
    uint16_t parentGate = 0;
    uint16_t gate = 0;
    int8_t parentNudge = 0;
    int8_t nudge = 0;
    uint8_t parentProbability = SequencerState::DEFAULT_PROBABILITY;
    uint8_t probability = SequencerState::DEFAULT_PROBABILITY;
    int16_t noteOffset = 0;
    int16_t velocityOffset = 0;
    int16_t gateOffset = 0;
    int16_t nudgeOffset = 0;
    int16_t probabilityOffset = 0;
    oc::note::sequencer::StepSequencerInheritedChord inheritedChord{};
    bool hasMicroSequence = false;
    bool hasCycleStates = false;
};

struct SequencerContentPlaybackProjection {
    bool visible = false;
    bool active = false;
    uint8_t step = 0;
};

struct SequencerChildContentSummary {
    bool enabled = true;
    SequencerGraphNodeId nodeId = SequencerContentStepProjection::INVALID_ID;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint16_t gate = 0;
    int8_t nudge = 0;
    uint8_t probability = SequencerState::DEFAULT_PROBABILITY;
    oc::note::sequencer::StepSequencerVariationRanges localVariation{};
};

SequencerGraphNodeId activeContentStepNodeId(
    const SequencerState& sequencer,
    uint8_t step
);
SequencerContentStepProjection resolveActiveContentStepProjection(
    const SequencerState& sequencer,
    uint8_t step,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
SequencerContentStepProjection resolveActiveContentOwnerProjection(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
SequencerContentStepProjection resolveContentFrameOwnerProjection(
    const SequencerState& sequencer,
    uint8_t frameDepth,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
SequencerContentPlaybackProjection resolveActiveContentPlaybackProjection(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
bool resolveRepresentativeChildContentNote(
    const SequencerState& sequencer,
    const SequencerContentStepProjection& projection,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    uint8_t& outNote
);
bool resolveRepresentativeChildContentSummary(
    const SequencerState& sequencer,
    const SequencerContentStepProjection& projection,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    SequencerChildContentSummary& outSummary
);
bool stepContentProjectionHasAnyChild(
    const SequencerContentStepProjection& projection
);
bool stepContentProjectionHasChild(
    const SequencerContentStepProjection& projection,
    StepContentChildKind childKind
);
int16_t stepContentProjectionOffsetForProperty(
    const SequencerContentStepProjection& projection,
    StepProperty property
);
StepContentCreationAvailability activeContentChildCreationAvailability(
    const SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind,
    uint8_t length
);
StepContentOpenResult openOrCreateActiveContentChild(
    SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind,
    uint8_t length
);
bool activeContentStepCanReceiveChildContent(
    const SequencerState& sequencer,
    uint8_t step
);
bool activeContentStepHasChildContent(
    const SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind
);
bool clipboardCanPasteActiveContentChild(
    const core::state::StructureClipboardState& clipboard,
    StepContentChildKind childKind
);
bool copyActiveContentChildToClipboard(
    const SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind,
    core::state::StructureClipboardState& clipboard
);
bool clearActiveContentChild(
    SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind
);
bool pasteActiveContentChildFromClipboard(
    SequencerState& sequencer,
    uint8_t step,
    StepContentChildKind childKind,
    const core::state::StructureClipboardState& clipboard
);
bool copyActiveContentChildrenToClipboard(
    const SequencerState& sequencer,
    uint8_t step,
    core::state::StructureClipboardState& clipboard
);
bool clearActiveContentChildren(SequencerState& sequencer, uint8_t step);
bool pasteActiveContentChildrenFromClipboard(
    SequencerState& sequencer,
    uint8_t step,
    const core::state::StructureClipboardState& clipboard
);

bool enterMicroSequenceContentView(
    SequencerState& sequencer,
    uint8_t parentStep,
    SequencerGraphSequenceId sequenceId
);
bool enterMicroSequenceContentView(
    SequencerState& sequencer,
    SequencerGraphNodeId ownerNodeId,
    SequencerGraphSequenceId sequenceId
);
bool enterCycleStatesContentView(
    SequencerState& sequencer,
    SequencerGraphNodeId ownerNodeId,
    SequencerGraphCycleSetId cycleSetId
);
bool leaveContentView(SequencerState& sequencer);
void refreshContentView(SequencerState& sequencer);
bool compactSequencerGraph(SequencerState& sequencer);

uint8_t activeContentLength(const SequencerState& sequencer);
uint8_t activeContentPageCount(const SequencerState& sequencer);
uint8_t normalizeActiveContentPage(const SequencerState& sequencer, uint8_t page);
uint8_t activeContentPageStartStep(const SequencerState& sequencer, uint8_t page);
uint8_t activeContentPageForStep(uint8_t step);
bool resolveActiveContentStepInPage(
    const SequencerState& sequencer,
    uint8_t page,
    uint8_t indexInPage,
    uint8_t& outStep
);
bool activeContentStepInPattern(const SequencerState& sequencer, uint8_t step);
bool rotateActiveContentSteps(SequencerState& sequencer, int offsetSteps);

bool toggleActiveContentStep(SequencerState& sequencer, uint8_t step);
bool activeContentStepEnabled(const SequencerState& sequencer, uint8_t step);
bool setActiveContentStepEnabled(SequencerState& sequencer, uint8_t step, bool enabled);
bool setActiveContentStepFromNormalized(
    SequencerState& sequencer,
    uint8_t step,
    StepProperty property,
    float normalized,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
bool resetActiveContentStepPropertyToDefault(
    SequencerState& sequencer,
    uint8_t step,
    StepProperty property
);
float activeContentStepPropertyToNormalized(
    const SequencerState& sequencer,
    uint8_t step,
    StepProperty property,
    SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);
bool resizeActiveMicroSequenceContent(SequencerState& sequencer, uint8_t length);
bool resizeActiveCycleStatesContent(SequencerState& sequencer, uint8_t length);

}  // namespace core::state::sequencer

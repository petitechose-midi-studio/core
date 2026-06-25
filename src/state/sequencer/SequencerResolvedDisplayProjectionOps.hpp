#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerVariation.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::state::sequencer {

struct SequencerResolvedVariationDisplayState {
    bool visible = false;
    bool rangeVisible = false;
    bool deltaVisible = false;
    StepProperty rangeProperty = StepProperty::NOTE;
    oc::note::sequencer::StepSequencerResolvedVariation resolved{};
};

struct SequencerResolvedStepDisplayState {
    bool valid = false;
    bool inPattern = false;
    bool enabled = false;
    bool playheadVisible = false;
    bool playing = false;
    bool probabilityCycleActive = false;

    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t probability = SequencerState::DEFAULT_PROBABILITY;
    uint16_t gate = 0;
    int8_t nudge = 0;

    bool childContentContext = false;
    int16_t childContentOffset = 0;
    bool childContentNoteOffsetUsesScaleDegrees = false;
    bool childPitchSummaryVisible = false;
    uint8_t childPitchSummaryNote = 0;

    SequencerGraphNodeId nodeId = SequencerContentStepProjection::INVALID_ID;
    SequencerGraphNodeId runtimeNodeId = SequencerContentStepProjection::INVALID_ID;

    SequencerResolvedVariationDisplayState variation{};
};

struct SequencerResolvedDisplayProjectionContext {
    const SequencerState* sequencer = nullptr;
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings{};
    StepProperty activeProperty = StepProperty::NOTE;
    bool childContext = false;
    uint8_t length = 0;
    bool probabilityCycleMaskActive = false;
    bool effectiveScaleFeedbackRelevant = false;
    bool telemetryFeedbackRelevant = false;
    SequencerContentPlaybackProjection contentPlayback{};
    const oc::note::sequencer::StepSequencerGraph* graph = nullptr;
    oc::note::sequencer::StepSequencerVariationRanges inheritedLocalVariation{};
};

SequencerResolvedDisplayProjectionContext makeSequencerResolvedDisplayProjectionContext(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings,
    StepProperty activeProperty
);

SequencerResolvedStepDisplayState buildSequencerResolvedStepDisplayState(
    const SequencerResolvedDisplayProjectionContext& context,
    uint8_t absoluteStep,
    bool stepInlineEditActive
);

oc::note::sequencer::StepSequencerStepValues sequencerResolvedStepDisplayValues(
    const SequencerResolvedStepDisplayState& step
);

}  // namespace core::state::sequencer

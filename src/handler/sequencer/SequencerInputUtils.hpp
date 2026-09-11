#pragma once

#include "handler/common/EncoderDefaults.hpp"
#include "state/sequencer/SequencerValueMapping.hpp"
#include "state/shared/NormalizedValue.hpp"
#include "state/sequencer/SequencerPitchEditAuthority.hpp"

/**
 * @file SequencerInputUtils.hpp
 * @brief Shared helpers for sequencer input value conversions.
 */

#include <algorithm>
#include <array>
#include <cstdint>

#include "state/sequencer/SequencerState.hpp"

namespace core::handler::sequencer::input_utils {

namespace normalized = core::state::normalized;
namespace pitch_edit = core::state::sequencer::pitch_edit;
namespace value_mapping = core::state::sequencer::value_mapping;
namespace encoder_defaults = core::handler::encoder_defaults;

using StepProperty = core::state::sequencer::StepProperty;
using SequencerState = core::state::sequencer::SequencerState;

// Matches the previous "16 ticks/step on macro encoders" feel, but in physical turns.
inline constexpr float NOTE_NORMALIZED_TURNS = 64.0f / 3.0f;
inline constexpr float GATE_NORMALIZED_TURNS = 4.0f;
inline constexpr int SWING_OFFSET_MIN =
    core::state::sequencer::SequencerPatternState::MIN_PATTERN_SWING_OFFSET_PERCENT;
inline constexpr int SWING_OFFSET_MAX =
    core::state::sequencer::SequencerPatternState::MAX_PATTERN_SWING_OFFSET_PERCENT;
inline constexpr const auto& STEPS_PER_BEAT_CHOICES =
    core::state::sequencer::PATTERN_STEPS_PER_BEAT_CHOICES;

struct StepPropertyEncoderConfig {
    uint8_t discreteSteps = 128;
    uint16_t discreteTicksPerStep = encoder_defaults::DEFAULT_DISCRETE_TICKS_PER_STEP;
    float normalizedTurns = encoder_defaults::DEFAULT_NORMALIZED_TURNS;
};

inline int8_t normalizedToSwingOffset(float normalized) {
    const int index = normalized::normalizedToInclusiveInt(normalized, SWING_OFFSET_MAX - SWING_OFFSET_MIN);
    return static_cast<int8_t>(SWING_OFFSET_MIN + index);
}

inline float swingOffsetToNormalized(int8_t offset) {
    const int clamped = std::clamp<int>(offset, SWING_OFFSET_MIN, SWING_OFFSET_MAX);
    return normalized::indexToNormalized(
        clamped - SWING_OFFSET_MIN,
        (SWING_OFFSET_MAX - SWING_OFFSET_MIN) + 1
    );
}

StepPropertyEncoderConfig encoderConfigForProperty(StepProperty property);

StepProperty drumStepProperty(
    core::state::sequencer::DrumSequencerProperty property
);

core::state::sequencer::DrumSequencerProperty drumPropertyForStepProperty(
    StepProperty property
);

StepPropertyEncoderConfig encoderConfigForDrumProperty(
    core::state::sequencer::DrumSequencerProperty property
);

float drumStepPropertyToNormalized(
    const core::state::sequencer::DrumSequencerState& drumUi,
    uint8_t lane,
    uint8_t step,
    core::state::sequencer::DrumSequencerProperty property
);

bool applyNormalizedToDrumStep(
    core::state::sequencer::DrumSequencerState& drumUi,
    uint8_t lane,
    uint8_t step,
    core::state::sequencer::DrumSequencerProperty property,
    float normalized
);

StepPropertyEncoderConfig encoderConfigForProperty(
    StepProperty property,
    core::state::sequencer::SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
);

inline uint8_t findStepsPerBeatChoiceIndex(uint8_t stepsPerBeat) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(STEPS_PER_BEAT_CHOICES.size()); ++i) {
        if (STEPS_PER_BEAT_CHOICES[i] == stepsPerBeat) return i;
    }
    return 1;
}

float quickControlToNormalized(
    const SequencerState& state,
    core::state::sequencer::PatternQuickControlItem item
);

inline StepPropertyEncoderConfig encoderConfigForQuickControl(
    core::state::sequencer::PatternQuickControlItem item
) {
    StepPropertyEncoderConfig config;
    switch (item) {
        case core::state::sequencer::PatternQuickControlItem::DIVISION:
            config.discreteSteps = static_cast<uint8_t>(STEPS_PER_BEAT_CHOICES.size());
            return config;
        case core::state::sequencer::PatternQuickControlItem::SWING:
            config.discreteSteps = static_cast<uint8_t>((SWING_OFFSET_MAX - SWING_OFFSET_MIN) + 1);
            return config;
        case core::state::sequencer::PatternQuickControlItem::NUDGE:
            config.discreteSteps = static_cast<uint8_t>((value_mapping::NUDGE_MAX - value_mapping::NUDGE_MIN) + 1);
            return config;
        case core::state::sequencer::PatternQuickControlItem::LENGTH:
        default:
            config.discreteSteps = SequencerState::MAX_STEPS;
            return config;
    }
}

void applyNormalizedToQuickControl(
    SequencerState& state,
    core::state::sequencer::PatternQuickControlItem item,
    float normalized
);

inline uint8_t variationRangeMaxForProperty(StepProperty property) {
    using Ranges = oc::note::sequencer::StepSequencerVariationRanges;

    switch (property) {
        case StepProperty::NOTE:
            return Ranges::MAX_PITCH_SEMITONES;
        case StepProperty::VELOCITY:
            return Ranges::MAX_VELOCITY;
        case StepProperty::GATE:
            return Ranges::MAX_GATE_PERCENT;
        case StepProperty::NUDGE:
            return Ranges::MAX_NUDGE;
        case StepProperty::PROBABILITY:
            return 0;
    }

    return 0;
}

inline uint8_t normalizedToVariationRange(StepProperty property, float normalized) {
    return static_cast<uint8_t>(
        normalized::normalizedToInclusiveInt(normalized, variationRangeMaxForProperty(property))
    );
}

inline float variationRangeToNormalized(StepProperty property, uint8_t range) {
    const uint8_t maxRange = variationRangeMaxForProperty(property);
    return normalized::indexToNormalized(std::min<uint8_t>(range, maxRange), static_cast<int>(maxRange) + 1);
}

inline StepPropertyEncoderConfig encoderConfigForVariationRange(StepProperty property) {
    StepPropertyEncoderConfig config;
    config.discreteSteps = static_cast<uint8_t>(variationRangeMaxForProperty(property) + 1U);
    return config;
}

inline uint8_t discreteStepsForProperty(StepProperty property) {
    return encoderConfigForProperty(property).discreteSteps;
}

inline uint16_t discreteTicksPerStepForProperty(StepProperty property) {
    return encoderConfigForProperty(property).discreteTicksPerStep;
}

inline float normalizedTurnsForProperty(StepProperty property) {
    return encoderConfigForProperty(property).normalizedTurns;
}

inline StepProperty stepEditRowToProperty(uint8_t row) {
    switch (row) {
        case 1:
            return StepProperty::VELOCITY;
        case 2:
            return StepProperty::GATE;
        case 3:
            return StepProperty::NUDGE;
        case 4:
            return StepProperty::PROBABILITY;
        case 0:
        default:
            return StepProperty::NOTE;
    }
}

inline float stepPropertyToNormalized(StepProperty property,
                                      uint8_t note,
                                      uint8_t velocity,
                                      uint16_t gatePercent,
                                      int8_t nudge,
                                      uint8_t probability = SequencerState::DEFAULT_PROBABILITY) {
    if (property == StepProperty::NOTE) {
        return normalized::indexToNormalized(note, 128);
    }

    if (property == StepProperty::VELOCITY) {
        return normalized::indexToNormalized(velocity, 128);
    }

    if (property == StepProperty::NUDGE) {
        return value_mapping::nudgeToNormalized(nudge);
    }

    if (property == StepProperty::PROBABILITY) {
        return value_mapping::probabilityToNormalized(probability);
    }

    return value_mapping::gatePercentToNormalized(gatePercent);
}

inline float stepPropertyToNormalized(const SequencerState& state, uint8_t step, StepProperty property) {
    if (step >= SequencerState::MAX_STEPS) return 0.0f;

    return stepPropertyToNormalized(
        property,
        state.pattern().note[step],
        state.pattern().velocity[step],
        state.pattern().gate[step],
        state.pattern().nudge[step],
        state.pattern().probability[step]
    );
}

inline float stepPropertyToNormalized(
    const SequencerState& state,
    uint8_t step,
    StepProperty property,
    core::state::sequencer::SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (step >= SequencerState::MAX_STEPS) return 0.0f;

    if (pitch_edit::usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        return normalized::indexToNormalized(
            pitch_edit::scaleDegreeIndexForNote(state.pattern().note[step], scaleSettings),
            pitch_edit::countScaleNotes(scaleSettings)
        );
    }

    return stepPropertyToNormalized(state, step, property);
}

inline bool applyNormalizedToStep(
    SequencerState& state,
    uint8_t step,
    StepProperty property,
    float normalized
) {
    const float value = normalized::clampNormalized(normalized);

    switch (property) {
        case StepProperty::NOTE:
            return state.setStepNoteAt(step, value_mapping::normalizedToMidi7(value));
        case StepProperty::VELOCITY:
            return state.setStepVelocityAt(step, value_mapping::normalizedToMidi7(value));
        case StepProperty::GATE:
            return state.setStepGateAt(step, value_mapping::normalizedToGatePercent(value));
        case StepProperty::NUDGE:
            return state.setStepNudgeAt(step, value_mapping::normalizedToNudge(value));
        case StepProperty::PROBABILITY:
            return state.setStepProbabilityAt(step, value_mapping::normalizedToProbability(value));
    }

    return false;
}

inline bool applyNormalizedToStep(
    SequencerState& state,
    uint8_t step,
    StepProperty property,
    float normalized,
    core::state::sequencer::SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (pitch_edit::usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        const int index = normalized::normalizedToIndex(normalized, pitch_edit::countScaleNotes(scaleSettings));
        return state.setStepNoteAt(step, pitch_edit::scaleNoteForDegreeIndex(index, scaleSettings));
    }

    return applyNormalizedToStep(state, step, property, normalized);
}

}  // namespace core::handler::sequencer::input_utils

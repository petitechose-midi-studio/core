#pragma once

/**
 * @file SequencerInputUtils.hpp
 * @brief Shared helpers for sequencer input value conversions.
 */

#include <algorithm>
#include <array>
#include <cstdint>

#include "state/sequencer/SequencerState.hpp"

namespace core::handler::sequencer::input_utils {

using StepProperty = core::state::sequencer::StepProperty;
using SequencerState = core::state::sequencer::SequencerState;

inline constexpr uint16_t DEFAULT_DISCRETE_TICKS_PER_STEP = 2;
inline constexpr float DEFAULT_NORMALIZED_TURNS = 0.0f;
// Matches the previous "16 ticks/step on macro encoders" feel, but in physical turns.
inline constexpr float NOTE_NORMALIZED_TURNS = 64.0f / 3.0f;
inline constexpr int PROBABILITY_MAX = 100;
inline constexpr int NUDGE_MIN = -50;
inline constexpr int NUDGE_MAX = 50;
inline constexpr std::array<uint8_t, 6> STEPS_PER_BEAT_CHOICES = {1, 2, 3, 4, 6, 8};

struct StepPropertyEncoderConfig {
    uint8_t discreteSteps = 128;
    uint16_t discreteTicksPerStep = DEFAULT_DISCRETE_TICKS_PER_STEP;
    float normalizedTurns = DEFAULT_NORMALIZED_TURNS;
};

inline float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

inline int normalizedToInclusiveInt(float normalized, int maxInclusive) {
    if (maxInclusive <= 0) return 0;

    const float value = clampNormalized(normalized);
    const int rounded = static_cast<int>(value * static_cast<float>(maxInclusive) + 0.5f);
    return std::clamp(rounded, 0, maxInclusive);
}

inline int normalizedToIndex(float normalized, int itemCount) {
    if (itemCount <= 1) return 0;
    return normalizedToInclusiveInt(normalized, itemCount - 1);
}

inline float indexToNormalized(int index, int itemCount) {
    if (itemCount <= 1) return 0.0f;

    const int clamped = std::clamp(index, 0, itemCount - 1);
    return static_cast<float>(clamped) / static_cast<float>(itemCount - 1);
}

inline uint8_t normalizedToMidi7(float normalized) {
    return static_cast<uint8_t>(normalizedToInclusiveInt(normalized, 127));
}

inline uint16_t normalizedToGatePercent(float normalized) {
    return static_cast<uint16_t>(
        normalizedToInclusiveInt(normalized, SequencerState::MAX_GATE_PERCENT)
    );
}

inline uint8_t normalizedToProbability(float normalized) {
    return static_cast<uint8_t>(normalizedToInclusiveInt(normalized, PROBABILITY_MAX));
}

inline float gatePercentToNormalized(uint16_t gatePercent) {
    return indexToNormalized(
        gatePercent,
        static_cast<int>(SequencerState::MAX_GATE_PERCENT) + 1
    );
}

inline float probabilityToNormalized(uint8_t probability) {
    return indexToNormalized(
        SequencerState::clampProbability(probability),
        PROBABILITY_MAX + 1
    );
}

inline int8_t normalizedToNudge(float normalized) {
    const int index = normalizedToInclusiveInt(normalized, NUDGE_MAX - NUDGE_MIN);
    return static_cast<int8_t>(NUDGE_MIN + index);
}

inline float nudgeToNormalized(int8_t nudge) {
    const int clamped = std::clamp<int>(nudge, NUDGE_MIN, NUDGE_MAX);
    return indexToNormalized(clamped - NUDGE_MIN, (NUDGE_MAX - NUDGE_MIN) + 1);
}

inline StepPropertyEncoderConfig encoderConfigForProperty(StepProperty property) {
    StepPropertyEncoderConfig config;

    if (property == StepProperty::GATE) {
        config.discreteSteps = static_cast<uint8_t>(SequencerState::MAX_GATE_PERCENT + 1);
        return config;
    }

    if (property == StepProperty::NUDGE) {
        config.discreteSteps = static_cast<uint8_t>((NUDGE_MAX - NUDGE_MIN) + 1);
        return config;
    }

    if (property == StepProperty::PROBABILITY) {
        config.discreteSteps = static_cast<uint8_t>(PROBABILITY_MAX + 1);
        return config;
    }

    if (property == StepProperty::NOTE) {
        config.normalizedTurns = NOTE_NORMALIZED_TURNS;
    }

    return config;
}

inline uint8_t findStepsPerBeatChoiceIndex(uint8_t stepsPerBeat) {
    for (uint8_t i = 0; i < static_cast<uint8_t>(STEPS_PER_BEAT_CHOICES.size()); ++i) {
        if (STEPS_PER_BEAT_CHOICES[i] == stepsPerBeat) return i;
    }
    return 1;
}

inline float quickControlToNormalized(
    const SequencerState& state,
    core::state::sequencer::PatternQuickControlItem item
) {
    switch (item) {
        case core::state::sequencer::PatternQuickControlItem::OFFSET:
            return 0.5f;
        case core::state::sequencer::PatternQuickControlItem::DIVISION:
            return indexToNormalized(
                findStepsPerBeatChoiceIndex(state.stepsPerBeat.get()),
                static_cast<int>(STEPS_PER_BEAT_CHOICES.size())
            );
        case core::state::sequencer::PatternQuickControlItem::LENGTH:
        default: {
            const uint8_t len = state.length.get();
            const uint8_t idx = (len > 0) ? static_cast<uint8_t>(len - 1) : 0;
            return indexToNormalized(idx, static_cast<int>(SequencerState::MAX_STEPS));
        }
    }
}

inline StepPropertyEncoderConfig encoderConfigForQuickControl(
    core::state::sequencer::PatternQuickControlItem item
) {
    StepPropertyEncoderConfig config;
    switch (item) {
        case core::state::sequencer::PatternQuickControlItem::DIVISION:
            config.discreteSteps = static_cast<uint8_t>(STEPS_PER_BEAT_CHOICES.size());
            return config;
        case core::state::sequencer::PatternQuickControlItem::LENGTH:
        default:
            config.discreteSteps = SequencerState::MAX_STEPS;
            return config;
    }
}

inline void applyNormalizedToQuickControl(
    SequencerState& state,
    core::state::sequencer::PatternQuickControlItem item,
    float normalized
) {
    const float value = clampNormalized(normalized);
    switch (item) {
        case core::state::sequencer::PatternQuickControlItem::DIVISION: {
            const int idx = normalizedToIndex(
                value,
                static_cast<int>(STEPS_PER_BEAT_CHOICES.size())
            );
            state.stepsPerBeat.set(STEPS_PER_BEAT_CHOICES[static_cast<size_t>(idx)]);
            return;
        }
        case core::state::sequencer::PatternQuickControlItem::LENGTH:
        default: {
            const int idx = normalizedToIndex(value, static_cast<int>(SequencerState::MAX_STEPS));
            state.length.set(static_cast<uint8_t>(idx + 1));
            return;
        }
    }
}

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
        normalizedToInclusiveInt(normalized, variationRangeMaxForProperty(property))
    );
}

inline float variationRangeToNormalized(StepProperty property, uint8_t range) {
    const uint8_t maxRange = variationRangeMaxForProperty(property);
    return indexToNormalized(std::min<uint8_t>(range, maxRange), static_cast<int>(maxRange) + 1);
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
        return indexToNormalized(note, 128);
    }

    if (property == StepProperty::VELOCITY) {
        return indexToNormalized(velocity, 128);
    }

    if (property == StepProperty::NUDGE) {
        return nudgeToNormalized(nudge);
    }

    if (property == StepProperty::PROBABILITY) {
        return probabilityToNormalized(probability);
    }

    return gatePercentToNormalized(gatePercent);
}

inline float stepPropertyToNormalized(const SequencerState& state, uint8_t step, StepProperty property) {
    if (step >= SequencerState::MAX_STEPS) return 0.0f;

    return stepPropertyToNormalized(
        property,
        state.note[step],
        state.velocity[step],
        state.gate[step],
        state.nudge[step],
        state.probability[step]
    );
}

inline bool applyNormalizedToStep(
    SequencerState& state,
    uint8_t step,
    StepProperty property,
    float normalized
) {
    const float value = clampNormalized(normalized);

    switch (property) {
        case StepProperty::NOTE:
            return state.setStepNoteAt(step, normalizedToMidi7(value));
        case StepProperty::VELOCITY:
            return state.setStepVelocityAt(step, normalizedToMidi7(value));
        case StepProperty::GATE:
            return state.setStepGateAt(step, normalizedToGatePercent(value));
        case StepProperty::NUDGE:
            return state.setStepNudgeAt(step, normalizedToNudge(value));
        case StepProperty::PROBABILITY:
            return state.setStepProbabilityAt(step, normalizedToProbability(value));
    }

    return false;
}

}  // namespace core::handler::sequencer::input_utils

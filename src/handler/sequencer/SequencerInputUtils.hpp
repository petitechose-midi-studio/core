#pragma once

/**
 * @file SequencerInputUtils.hpp
 * @brief Shared helpers for sequencer input value conversions.
 */

#include <algorithm>
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

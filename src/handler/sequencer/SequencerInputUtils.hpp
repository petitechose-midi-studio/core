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
inline constexpr float GATE_NORMALIZED_TURNS = 4.0f;
inline constexpr float GATE_UNIT_NORMALIZED_POINT = 0.5f;
inline constexpr int PROBABILITY_MAX = 100;
inline constexpr int NUDGE_MIN = -50;
inline constexpr int NUDGE_MAX = 50;
inline constexpr int SWING_OFFSET_MIN =
    core::state::sequencer::SequencerPatternState::MIN_PATTERN_SWING_OFFSET_PERCENT;
inline constexpr int SWING_OFFSET_MAX =
    core::state::sequencer::SequencerPatternState::MAX_PATTERN_SWING_OFFSET_PERCENT;
inline constexpr const auto& STEPS_PER_BEAT_CHOICES =
    core::state::sequencer::PATTERN_STEPS_PER_BEAT_CHOICES;

struct StepPropertyEncoderConfig {
    uint8_t discreteSteps = 128;
    uint16_t discreteTicksPerStep = DEFAULT_DISCRETE_TICKS_PER_STEP;
    float normalizedTurns = DEFAULT_NORMALIZED_TURNS;
};

float clampNormalized(float value);

int normalizedToInclusiveInt(float normalized, int maxInclusive);

int normalizedToIndex(float normalized, int itemCount);

float indexToNormalized(int index, int itemCount);

inline uint8_t normalizedToMidi7(float normalized) {
    return static_cast<uint8_t>(normalizedToInclusiveInt(normalized, 127));
}

inline uint16_t normalizedToGatePercent(float normalized) {
    const float value = clampNormalized(normalized);
    constexpr uint16_t unitGate = SequencerState::DEFAULT_GATE_PERCENT;
    constexpr uint16_t maxGate = SequencerState::MAX_GATE_PERCENT;
    if constexpr (maxGate <= unitGate) {
        return static_cast<uint16_t>(normalizedToInclusiveInt(value, maxGate));
    }

    if (value <= GATE_UNIT_NORMALIZED_POINT) {
        const float scaled = value / GATE_UNIT_NORMALIZED_POINT;
        return static_cast<uint16_t>(normalizedToInclusiveInt(scaled, unitGate));
    }

    const float scaled =
        (value - GATE_UNIT_NORMALIZED_POINT) / (1.0f - GATE_UNIT_NORMALIZED_POINT);
    const int extended =
        static_cast<int>(unitGate) +
        normalizedToInclusiveInt(scaled, static_cast<int>(maxGate - unitGate));
    return static_cast<uint16_t>(std::clamp(extended, 0, static_cast<int>(maxGate)));
}

inline uint8_t normalizedToProbability(float normalized) {
    return static_cast<uint8_t>(normalizedToInclusiveInt(normalized, PROBABILITY_MAX));
}

inline float gatePercentToNormalized(uint16_t gatePercent) {
    constexpr uint16_t unitGate = SequencerState::DEFAULT_GATE_PERCENT;
    constexpr uint16_t maxGate = SequencerState::MAX_GATE_PERCENT;
    const uint16_t clamped = SequencerState::clampGatePercent(gatePercent);
    if constexpr (maxGate <= unitGate) {
        return indexToNormalized(clamped, static_cast<int>(maxGate) + 1);
    }
    if (clamped <= unitGate) {
        return (static_cast<float>(clamped) / static_cast<float>(unitGate)) *
               GATE_UNIT_NORMALIZED_POINT;
    }
    const float extended =
        static_cast<float>(clamped - unitGate) / static_cast<float>(maxGate - unitGate);
    return GATE_UNIT_NORMALIZED_POINT +
           extended * (1.0f - GATE_UNIT_NORMALIZED_POINT);
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

inline int8_t normalizedToSwingOffset(float normalized) {
    const int index = normalizedToInclusiveInt(normalized, SWING_OFFSET_MAX - SWING_OFFSET_MIN);
    return static_cast<int8_t>(SWING_OFFSET_MIN + index);
}

float nudgeToNormalized(int8_t nudge);

inline float swingOffsetToNormalized(int8_t offset) {
    const int clamped = std::clamp<int>(offset, SWING_OFFSET_MIN, SWING_OFFSET_MAX);
    return indexToNormalized(
        clamped - SWING_OFFSET_MIN,
        (SWING_OFFSET_MAX - SWING_OFFSET_MIN) + 1
    );
}

StepPropertyEncoderConfig encoderConfigForProperty(StepProperty property);

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
StepProperty drumStepProperty(
    core::state::sequencer::DrumTrackUxPrototypeProperty property
);

core::state::sequencer::DrumTrackUxPrototypeProperty drumPropertyForStepProperty(
    StepProperty property
);

StepPropertyEncoderConfig encoderConfigForDrumProperty(
    core::state::sequencer::DrumTrackUxPrototypeProperty property
);

float drumStepPropertyToNormalized(
    const core::state::sequencer::DrumTrackUxPrototypeState& prototype,
    uint8_t lane,
    uint8_t step,
    core::state::sequencer::DrumTrackUxPrototypeProperty property
);

bool applyNormalizedToDrumStep(
    core::state::sequencer::DrumTrackUxPrototypeState& prototype,
    uint8_t lane,
    uint8_t step,
    core::state::sequencer::DrumTrackUxPrototypeProperty property,
    float normalized
);
#endif

inline bool usesScaleDegreePitchEdit(
    StepProperty property,
    core::state::sequencer::SequencerPitchEditMode mode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    return property == StepProperty::NOTE &&
           core::state::sequencer::pitchContextUsesScaleDegrees(
               mode,
               scaleSettings
           );
}

inline int countScaleNotes(oc::note::sequencer::StepSequencerScaleSettings scaleSettings) {
    scaleSettings.clamp();
    int count = 0;
    for (int note = 0; note <= 127; ++note) {
        if (oc::note::sequencer::scaleContainsNote(scaleSettings, static_cast<uint8_t>(note))) {
            ++count;
        }
    }
    return std::max(count, 1);
}

inline int scaleDegreeIndexForNote(
    uint8_t note,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    const uint8_t resolved =
        oc::note::sequencer::resolveScaleNote(note, scaleSettings).outputNote;
    int index = 0;
    for (int candidate = 0; candidate <= 127; ++candidate) {
        if (!oc::note::sequencer::scaleContainsNote(
                scaleSettings,
                static_cast<uint8_t>(candidate)
            )) {
            continue;
        }
        if (candidate >= resolved) return index;
        ++index;
    }
    return std::max(0, index - 1);
}

inline uint8_t scaleNoteForDegreeIndex(
    int index,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    const int clampedIndex = std::clamp(index, 0, countScaleNotes(scaleSettings) - 1);
    int current = 0;
    for (int note = 0; note <= 127; ++note) {
        if (!oc::note::sequencer::scaleContainsNote(scaleSettings, static_cast<uint8_t>(note))) {
            continue;
        }
        if (current == clampedIndex) return static_cast<uint8_t>(note);
        ++current;
    }
    return 0;
}

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
            config.discreteSteps = static_cast<uint8_t>((NUDGE_MAX - NUDGE_MIN) + 1);
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
        state.pattern.note[step],
        state.pattern.velocity[step],
        state.pattern.gate[step],
        state.pattern.nudge[step],
        state.pattern.probability[step]
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

    if (usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        return indexToNormalized(
            scaleDegreeIndexForNote(state.pattern.note[step], scaleSettings),
            countScaleNotes(scaleSettings)
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

inline bool applyNormalizedToStep(
    SequencerState& state,
    uint8_t step,
    StepProperty property,
    float normalized,
    core::state::sequencer::SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    if (usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        const int index = normalizedToIndex(normalized, countScaleNotes(scaleSettings));
        return state.setStepNoteAt(step, scaleNoteForDegreeIndex(index, scaleSettings));
    }

    return applyNormalizedToStep(state, step, property, normalized);
}

}  // namespace core::handler::sequencer::input_utils

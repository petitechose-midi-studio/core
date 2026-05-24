#pragma once

#include <algorithm>
#include <cstdint>

#include "state/sequencer/SequencerScaleState.hpp"

namespace core::state::sequencer::scale_catalog {

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleType;

inline constexpr int ROOT_COUNT = 12;
inline constexpr int SCALE_TYPE_COUNT = 14;
inline constexpr int CONSTRAINT_MODE_COUNT = 4;
inline constexpr int PATTERN_SCALE_POLICY_COUNT = 2;
inline constexpr int PITCH_EDIT_MODE_COUNT = 2;

inline constexpr const char* ROOT_LABELS[ROOT_COUNT] = {
    "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B",
};

inline constexpr StepSequencerScaleType SCALE_TYPE_VALUES[SCALE_TYPE_COUNT] = {
    StepSequencerScaleType::Chromatic,
    StepSequencerScaleType::Major,
    StepSequencerScaleType::NaturalMinor,
    StepSequencerScaleType::HarmonicMinor,
    StepSequencerScaleType::MelodicMinor,
    StepSequencerScaleType::Dorian,
    StepSequencerScaleType::Phrygian,
    StepSequencerScaleType::Lydian,
    StepSequencerScaleType::Mixolydian,
    StepSequencerScaleType::Locrian,
    StepSequencerScaleType::MajorPentatonic,
    StepSequencerScaleType::MinorPentatonic,
    StepSequencerScaleType::Blues,
    StepSequencerScaleType::WholeTone,
};

inline constexpr const char* SCALE_TYPE_LABELS[SCALE_TYPE_COUNT] = {
    "Chromatic",
    "Major",
    "Nat Minor",
    "Harm Minor",
    "Mel Minor",
    "Dorian",
    "Phrygian",
    "Lydian",
    "Mixolydian",
    "Locrian",
    "Maj Pent",
    "Min Pent",
    "Blues",
    "Whole Tone",
};

inline constexpr StepSequencerScaleConstraintMode CONSTRAINT_MODE_VALUES[CONSTRAINT_MODE_COUNT] = {
    StepSequencerScaleConstraintMode::Free,
    StepSequencerScaleConstraintMode::ConstrainNearest,
    StepSequencerScaleConstraintMode::ConstrainUp,
    StepSequencerScaleConstraintMode::ConstrainDown,
};

inline constexpr const char* CONSTRAINT_MODE_LABELS[CONSTRAINT_MODE_COUNT] = {
    "Free", "Nearest", "Up", "Down",
};

inline constexpr SequencerPatternScalePolicy PATTERN_SCALE_POLICY_VALUES[PATTERN_SCALE_POLICY_COUNT] = {
    SequencerPatternScalePolicy::INHERIT_PROJECT,
    SequencerPatternScalePolicy::OVERRIDE,
};

inline constexpr const char* PATTERN_SCALE_POLICY_LABELS[PATTERN_SCALE_POLICY_COUNT] = {
    "Inherit", "Override",
};

inline constexpr SequencerPitchEditMode PITCH_EDIT_MODE_VALUES[PITCH_EDIT_MODE_COUNT] = {
    SequencerPitchEditMode::CHROMATIC,
    SequencerPitchEditMode::SCALE_DEGREES,
};

inline constexpr const char* PITCH_EDIT_MODE_LABELS[PITCH_EDIT_MODE_COUNT] = {
    "Chromatic", "Scale Deg",
};

template <typename T, int N>
inline int choiceIndex(const T& value, const T (&choices)[N], int fallback = 0) {
    for (int i = 0; i < N; ++i) {
        if (choices[i] == value) return i;
    }
    return std::clamp(fallback, 0, N - 1);
}

inline int scaleTypeIndex(StepSequencerScaleType type) {
    return choiceIndex(type, SCALE_TYPE_VALUES, 0);
}

inline int constraintModeIndex(StepSequencerScaleConstraintMode mode) {
    return choiceIndex(mode, CONSTRAINT_MODE_VALUES, 0);
}

inline int pitchEditModeIndex(SequencerPitchEditMode mode) {
    return choiceIndex(mode, PITCH_EDIT_MODE_VALUES, 0);
}

inline const char* rootLabel(uint8_t root) {
    return ROOT_LABELS[std::clamp<int>(root, 0, ROOT_COUNT - 1)];
}

inline const char* scaleTypeLabel(StepSequencerScaleType type) {
    return SCALE_TYPE_LABELS[scaleTypeIndex(type)];
}

inline const char* constraintModeLabel(StepSequencerScaleConstraintMode mode) {
    return CONSTRAINT_MODE_LABELS[constraintModeIndex(mode)];
}

inline const char* pitchEditModeLabel(SequencerPitchEditMode mode) {
    return PITCH_EDIT_MODE_LABELS[pitchEditModeIndex(mode)];
}

}  // namespace core::state::sequencer::scale_catalog

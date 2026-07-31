#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerScale.hpp>

namespace core::state::sequencer {

enum class SequencerPatternScalePolicy : uint8_t {
    INHERIT_PROJECT = 0,
    OVERRIDE = 1,
};

enum class SequencerPitchEditMode : uint8_t {
    // Follow the effective musical context: constrained non-chromatic scales
    // use degrees; Chromatic and Free contexts use semitones.
    FOLLOW_SCALE = 0,
    CHROMATIC = 1,
};

inline bool isPatternScaleOverride(SequencerPatternScalePolicy policy) {
    return policy == SequencerPatternScalePolicy::OVERRIDE;
}

inline SequencerPatternScalePolicy sanitizePatternScalePolicy(uint8_t value) {
    if (value > static_cast<uint8_t>(SequencerPatternScalePolicy::OVERRIDE)) {
        return SequencerPatternScalePolicy::INHERIT_PROJECT;
    }
    return static_cast<SequencerPatternScalePolicy>(value);
}

inline SequencerPitchEditMode sanitizePitchEditMode(uint8_t value) {
    if (value > static_cast<uint8_t>(SequencerPitchEditMode::CHROMATIC)) {
        return SequencerPitchEditMode::FOLLOW_SCALE;
    }
    return static_cast<SequencerPitchEditMode>(value);
}

inline bool validPitchEditMode(uint8_t value) {
    return value <= static_cast<uint8_t>(SequencerPitchEditMode::CHROMATIC);
}

inline bool pitchContextUsesScaleDegrees(
    SequencerPitchEditMode mode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    return mode == SequencerPitchEditMode::FOLLOW_SCALE &&
           scaleSettings.isConstrained();
}

inline oc::note::sequencer::StepSequencerScaleSettings sanitizedScaleSettings(
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    settings.clamp();
    return settings;
}

constexpr oc::note::sequencer::StepSequencerScaleSettings defaultProjectScaleSettings() {
    return oc::note::sequencer::StepSequencerScaleSettings{
        .root = 5,
        .type = oc::note::sequencer::StepSequencerScaleType::HarmonicMinor,
        .mode = oc::note::sequencer::StepSequencerScaleConstraintMode::ConstrainNearest,
    };
}

inline oc::note::sequencer::StepSequencerScaleSettings resolveEffectiveScaleSettings(
    oc::note::sequencer::StepSequencerScaleSettings projectScale,
    SequencerPatternScalePolicy policy,
    oc::note::sequencer::StepSequencerScaleSettings overrideScale
) {
    projectScale.clamp();
    overrideScale.clamp();
    return isPatternScaleOverride(policy) ? overrideScale : projectScale;
}

}  // namespace core::state::sequencer

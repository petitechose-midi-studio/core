#include "handler/sequencer/SequencerInputUtils.hpp"

#include <config/PlatformCompat.hpp>

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
#include "state/sequencer/DrumPatternState.hpp"
#endif
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::handler::sequencer::input_utils {

FLASHMEM float clampNormalized(float value) {
    return std::clamp(value, 0.0f, 1.0f);
}

FLASHMEM int normalizedToInclusiveInt(float normalized, int maxInclusive) {
    if (maxInclusive <= 0) return 0;

    const float value = clampNormalized(normalized);
    const int rounded = static_cast<int>(value * static_cast<float>(maxInclusive) + 0.5f);
    return std::clamp(rounded, 0, maxInclusive);
}

FLASHMEM int normalizedToIndex(float normalized, int itemCount) {
    if (itemCount <= 1) return 0;
    return normalizedToInclusiveInt(normalized, itemCount - 1);
}

FLASHMEM float indexToNormalized(int index, int itemCount) {
    if (itemCount <= 1) return 0.0f;

    const int clamped = std::clamp(index, 0, itemCount - 1);
    return static_cast<float>(clamped) / static_cast<float>(itemCount - 1);
}

FLASHMEM float nudgeToNormalized(int8_t nudge) {
    const int clamped = std::clamp<int>(nudge, NUDGE_MIN, NUDGE_MAX);
    return indexToNormalized(
        clamped - NUDGE_MIN,
        (NUDGE_MAX - NUDGE_MIN) + 1
    );
}

FLASHMEM StepPropertyEncoderConfig encoderConfigForProperty(
    StepProperty property
) {
    StepPropertyEncoderConfig config;

    if (property == StepProperty::GATE) {
        config.discreteSteps = 0;
        config.normalizedTurns = GATE_NORMALIZED_TURNS;
        return config;
    }

    if (property == StepProperty::NUDGE) {
        config.discreteSteps = static_cast<uint8_t>(
            (NUDGE_MAX - NUDGE_MIN) + 1
        );
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

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
using DrumProperty = core::state::sequencer::DrumTrackUxPrototypeProperty;

FLASHMEM StepProperty drumStepProperty(DrumProperty property) {
    switch (property) {
        case DrumProperty::PROBABILITY: return StepProperty::PROBABILITY;
        case DrumProperty::GATE: return StepProperty::GATE;
        case DrumProperty::NUDGE: return StepProperty::NUDGE;
        case DrumProperty::STATE:
        case DrumProperty::VELOCITY:
        case DrumProperty::COUNT:
        default: return StepProperty::VELOCITY;
    }
}

FLASHMEM DrumProperty drumPropertyForStepProperty(StepProperty property) {
    switch (property) {
        case StepProperty::PROBABILITY: return DrumProperty::PROBABILITY;
        case StepProperty::GATE: return DrumProperty::GATE;
        case StepProperty::NUDGE: return DrumProperty::NUDGE;
        case StepProperty::NOTE:
        case StepProperty::VELOCITY:
        default: return DrumProperty::VELOCITY;
    }
}

FLASHMEM StepPropertyEncoderConfig encoderConfigForDrumProperty(
    DrumProperty property
) {
    if (property != DrumProperty::STATE) {
        return encoderConfigForProperty(drumStepProperty(property));
    }
    StepPropertyEncoderConfig config;
    config.discreteSteps = 2U;
    return config;
}

FLASHMEM float drumStepPropertyToNormalized(
    const core::state::sequencer::DrumTrackUxPrototypeState& prototype,
    uint8_t laneIndex,
    uint8_t step,
    DrumProperty property
) {
    if (!prototype.drumTrack || laneIndex >= prototype.LANE_COUNT ||
        step >= prototype.MAX_STEPS) {
        return 0.0f;
    }
    if (property == DrumProperty::STATE) {
        return prototype.drumTrack->pattern.stepEnabled(laneIndex, step)
            ? 1.0f
            : 0.0f;
    }

    const auto& descriptor = prototype.drumTrack->kit.lanes[laneIndex];
    const auto& lane = prototype.drumTrack->pattern.lanes[laneIndex];
    return stepPropertyToNormalized(
        drumStepProperty(property),
        descriptor.midiNote,
        lane.velocity[step],
        lane.gate[step],
        lane.nudge[step],
        lane.probability[step]
    );
}

FLASHMEM bool applyNormalizedToDrumStep(
    core::state::sequencer::DrumTrackUxPrototypeState& prototype,
    uint8_t lane,
    uint8_t step,
    DrumProperty property,
    float normalized
) {
    switch (property) {
        case DrumProperty::STATE:
            return prototype.setStepEnabled(
                lane,
                step,
                clampNormalized(normalized) >= 0.5f
            );
        case DrumProperty::PROBABILITY:
            return prototype.setStepProbability(
                lane,
                step,
                normalizedToProbability(normalized)
            );
        case DrumProperty::GATE:
            return prototype.setStepGate(
                lane,
                step,
                normalizedToGatePercent(normalized)
            );
        case DrumProperty::NUDGE:
            return prototype.setStepNudge(
                lane,
                step,
                normalizedToNudge(normalized)
            );
        case DrumProperty::VELOCITY:
        case DrumProperty::COUNT:
        default:
            return prototype.setStepVelocity(
                lane,
                step,
                normalizedToMidi7(normalized)
            );
    }
}
#endif

FLASHMEM StepPropertyEncoderConfig encoderConfigForProperty(
    StepProperty property,
    core::state::sequencer::SequencerPitchEditMode pitchEditMode,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    auto config = encoderConfigForProperty(property);
    if (usesScaleDegreePitchEdit(property, pitchEditMode, scaleSettings)) {
        config.discreteSteps = static_cast<uint8_t>(
            std::min(countScaleNotes(scaleSettings), 255)
        );
    }
    return config;
}

FLASHMEM float quickControlToNormalized(
    const SequencerState& state,
    core::state::sequencer::PatternQuickControlItem item
) {
    const auto& pattern = core::state::sequencer::authoringPattern(state);
    switch (item) {
        case core::state::sequencer::PatternQuickControlItem::OFFSET:
            return 0.5f;
        case core::state::sequencer::PatternQuickControlItem::SWING:
            return swingOffsetToNormalized(pattern.swingOffsetPercent.get());
        case core::state::sequencer::PatternQuickControlItem::NUDGE:
            return nudgeToNormalized(pattern.patternNudgePercent.get());
        case core::state::sequencer::PatternQuickControlItem::DIVISION:
            return indexToNormalized(
                findStepsPerBeatChoiceIndex(pattern.stepsPerBeat.get()),
                static_cast<int>(STEPS_PER_BEAT_CHOICES.size())
            );
        case core::state::sequencer::PatternQuickControlItem::LENGTH:
        default: {
            const uint8_t len = pattern.length.get();
            const uint8_t idx = (len > 0) ? static_cast<uint8_t>(len - 1) : 0;
            return indexToNormalized(idx, static_cast<int>(SequencerState::MAX_STEPS));
        }
    }
}

FLASHMEM void applyNormalizedToQuickControl(
    SequencerState& state,
    core::state::sequencer::PatternQuickControlItem item,
    float normalized
) {
    const float value = clampNormalized(normalized);
    auto& pattern = core::state::sequencer::authoringPattern(state);
    const bool detached = &pattern != &state.pattern;
    switch (item) {
        case core::state::sequencer::PatternQuickControlItem::DIVISION: {
            const int idx = normalizedToIndex(
                value,
                static_cast<int>(STEPS_PER_BEAT_CHOICES.size())
            );
            pattern.stepsPerBeat.set(STEPS_PER_BEAT_CHOICES[static_cast<size_t>(idx)]);
            return;
        }
        case core::state::sequencer::PatternQuickControlItem::SWING:
            if (detached) pattern.setPatternSwingOffsetPercent(normalizedToSwingOffset(value));
            else state.setPatternSwingOffsetPercent(normalizedToSwingOffset(value));
            return;
        case core::state::sequencer::PatternQuickControlItem::NUDGE:
            if (detached) pattern.setPatternNudgePercent(normalizedToNudge(value));
            else state.setPatternNudgePercent(normalizedToNudge(value));
            return;
        case core::state::sequencer::PatternQuickControlItem::LENGTH:
        default: {
            const int idx = normalizedToIndex(value, static_cast<int>(SequencerState::MAX_STEPS));
            pattern.setContentLength(static_cast<uint8_t>(idx + 1));
            return;
        }
    }
}

}  // namespace core::handler::sequencer::input_utils

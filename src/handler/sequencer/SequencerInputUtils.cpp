#include "handler/sequencer/SequencerInputUtils.hpp"

#include <config/PlatformCompat.hpp>

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
    switch (item) {
        case core::state::sequencer::PatternQuickControlItem::OFFSET:
            return 0.5f;
        case core::state::sequencer::PatternQuickControlItem::SWING:
            return swingOffsetToNormalized(state.pattern.swingOffsetPercent.get());
        case core::state::sequencer::PatternQuickControlItem::NUDGE:
            return nudgeToNormalized(state.pattern.patternNudgePercent.get());
        case core::state::sequencer::PatternQuickControlItem::DIVISION:
            return indexToNormalized(
                findStepsPerBeatChoiceIndex(state.pattern.stepsPerBeat.get()),
                static_cast<int>(STEPS_PER_BEAT_CHOICES.size())
            );
        case core::state::sequencer::PatternQuickControlItem::LENGTH:
        default: {
            const uint8_t len = state.pattern.length.get();
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
    switch (item) {
        case core::state::sequencer::PatternQuickControlItem::DIVISION: {
            const int idx = normalizedToIndex(
                value,
                static_cast<int>(STEPS_PER_BEAT_CHOICES.size())
            );
            state.pattern.stepsPerBeat.set(STEPS_PER_BEAT_CHOICES[static_cast<size_t>(idx)]);
            return;
        }
        case core::state::sequencer::PatternQuickControlItem::SWING:
            state.setPatternSwingOffsetPercent(normalizedToSwingOffset(value));
            return;
        case core::state::sequencer::PatternQuickControlItem::NUDGE:
            state.setPatternNudgePercent(normalizedToNudge(value));
            return;
        case core::state::sequencer::PatternQuickControlItem::LENGTH:
        default: {
            const int idx = normalizedToIndex(value, static_cast<int>(SequencerState::MAX_STEPS));
            state.pattern.setContentLength(static_cast<uint8_t>(idx + 1));
            return;
        }
    }
}

}  // namespace core::handler::sequencer::input_utils

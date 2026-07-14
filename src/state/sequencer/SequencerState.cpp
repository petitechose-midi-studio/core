#include "state/sequencer/SequencerState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

FLASHMEM SequencerPatternState::~SequencerPatternState() = default;

FLASHMEM uint8_t SequencerPatternState::variationRangeForProperty(
    StepProperty property
) const {
    switch (property) {
        case StepProperty::NOTE:
            return variationRanges.pitchSemitones;
        case StepProperty::VELOCITY:
            return variationRanges.velocity;
        case StepProperty::GATE:
            return variationRanges.gatePercent;
        case StepProperty::NUDGE:
            return variationRanges.nudge;
        case StepProperty::PROBABILITY:
            return 0;
    }

    return 0;
}

FLASHMEM bool SequencerPatternState::setVariationRangeForProperty(
    StepProperty property,
    uint8_t range
) {
    auto next = variationRanges;
    switch (property) {
        case StepProperty::NOTE:
            next.pitchSemitones = range;
            break;
        case StepProperty::VELOCITY:
            next.velocity = range;
            break;
        case StepProperty::GATE:
            next.gatePercent = range;
            break;
        case StepProperty::NUDGE:
            next.nudge = range;
            break;
        case StepProperty::PROBABILITY:
            return false;
    }

    next.clamp();
    if (next.pitchSemitones == variationRanges.pitchSemitones &&
        next.velocity == variationRanges.velocity &&
        next.gatePercent == variationRanges.gatePercent &&
        next.nudge == variationRanges.nudge) {
        return false;
    }

    variationRanges = next;
    bumpPatternVariationRevision();
    return true;
}

FLASHMEM bool SequencerPatternState::setPatternVariationRanges(
    oc::note::sequencer::StepSequencerVariationRanges ranges
) {
    ranges.clamp();
    if (ranges.pitchSemitones == variationRanges.pitchSemitones &&
        ranges.velocity == variationRanges.velocity &&
        ranges.gatePercent == variationRanges.gatePercent &&
        ranges.nudge == variationRanges.nudge) {
        return false;
    }

    variationRanges = ranges;
    bumpPatternVariationRevision();
    return true;
}

FLASHMEM bool SequencerPatternState::setPatternScalePolicy(
    SequencerPatternScalePolicy policy
) {
    if (scalePolicy == policy) return false;
    scalePolicy = policy;
    bumpPatternScaleRevision();
    return true;
}

FLASHMEM bool SequencerPatternState::setPatternScaleOverride(
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    settings.clamp();
    auto current = scaleOverride;
    current.clamp();
    if (current.root == settings.root &&
        current.type == settings.type &&
        current.mode == settings.mode) {
        return false;
    }

    scaleOverride = settings;
    bumpPatternScaleRevision();
    return true;
}

FLASHMEM bool SequencerPatternState::setPitchEditMode(SequencerPitchEditMode mode) {
    if (pitchEditMode == mode) return false;
    pitchEditMode = mode;
    bumpPatternScaleRevision();
    return true;
}

FLASHMEM bool SequencerPatternState::setPatternSwingOffsetPercent(int value) {
    const int8_t clamped = clampPatternSwingOffsetPercent(value);
    if (swingOffsetPercent.get() == clamped) return false;
    swingOffsetPercent.set(clamped);
    bumpPatternTimingRevision();
    return true;
}

FLASHMEM bool SequencerPatternState::setPatternNudgePercent(int value) {
    const int8_t clamped = clampPatternNudgePercent(value);
    if (patternNudgePercent.get() == clamped) return false;
    patternNudgePercent.set(clamped);
    bumpPatternTimingRevision();
    return true;
}

FLASHMEM SequencerState::SequencerState() = default;
FLASHMEM SequencerState::~SequencerState() = default;

FLASHMEM void SequencerState::invalidateVariationTelemetry() {
    lastResolvedVariation = {};
    cycleVariationTelemetry.reset();
    expandedVariationTelemetry.reset();
    variationTelemetryRevision.set(variationTelemetryRevision.get() + 1);
}

FLASHMEM void SequencerState::invalidateStepVariationTelemetry(uint8_t step) {
    if (step >= MAX_STEPS) return;
    if (!cycleVariationTelemetry.validMask.test(step) &&
        lastResolvedVariation.stepIndex != step) {
        return;
    }

    cycleVariationTelemetry.validMask.setBit(step, false);
    cycleVariationTelemetry.triggeredMask.setBit(step, false);
    cycleVariationTelemetry.scaleInMask.setBit(step, false);
    cycleVariationTelemetry.scaleConstrainedMask.setBit(step, false);
    if (lastResolvedVariation.stepIndex == step) {
        lastResolvedVariation = {};
    }
    if (expandedVariationTelemetry.rootStepIndex == step) {
        expandedVariationTelemetry.reset();
    }
    variationTelemetryRevision.set(variationTelemetryRevision.get() + 1);
}

FLASHMEM bool SequencerState::setVariationRangeForProperty(
    StepProperty property,
    uint8_t range
) {
    if (!pattern.setVariationRangeForProperty(property, range)) return false;
    invalidateVariationTelemetry();
    return true;
}

FLASHMEM bool SequencerState::setPatternVariationRanges(
    oc::note::sequencer::StepSequencerVariationRanges ranges
) {
    if (!pattern.setPatternVariationRanges(ranges)) return false;
    invalidateVariationTelemetry();
    return true;
}

FLASHMEM bool SequencerState::setPatternScalePolicy(SequencerPatternScalePolicy policy) {
    if (!pattern.setPatternScalePolicy(policy)) return false;
    invalidateVariationTelemetry();
    return true;
}

FLASHMEM bool SequencerState::setPatternScaleOverride(
    oc::note::sequencer::StepSequencerScaleSettings settings
) {
    if (!pattern.setPatternScaleOverride(settings)) return false;
    invalidateVariationTelemetry();
    return true;
}

FLASHMEM bool SequencerState::setPitchEditMode(SequencerPitchEditMode mode) {
    if (!pattern.setPitchEditMode(mode)) return false;
    invalidateVariationTelemetry();
    return true;
}

FLASHMEM bool SequencerState::setPatternSwingOffsetPercent(int value) {
    if (!pattern.setPatternSwingOffsetPercent(value)) return false;
    invalidateVariationTelemetry();
    return true;
}

FLASHMEM bool SequencerState::setPatternNudgePercent(int value) {
    if (!pattern.setPatternNudgePercent(value)) return false;
    invalidateVariationTelemetry();
    return true;
}

FLASHMEM void SequencerPatternState::reset() {
    oc::note::sequencer::StepSequencerState::reset();
    bumpStepDataRevision();
    variationRanges = {};
    bumpPatternVariationRevision();
    scalePolicy = SequencerPatternScalePolicy::INHERIT_PROJECT;
    scaleOverride = {};
    pitchEditMode = SequencerPitchEditMode::CHROMATIC;
    bumpPatternScaleRevision();
    swingOffsetPercent.set(0);
    patternNudgePercent.set(0);
    bumpPatternTimingRevision();
    graph.reset();
    bumpGraphRevision();
    ccLanes.reset();
    bumpCcLaneRevision();
}

FLASHMEM void SequencerState::reset() {
    pattern.reset();
    page.set(0);
    focusedStep.set(0);
    playheadStep.set(-1);
    playheadStepTickOffset.set(0);
    playheadStepTicks = 1;
    probabilityCycleRevision.set(0);
    probabilityCycleMask = {};
    probabilityCycleIndex = 0;
    lastResolvedVariation = {};
    cycleVariationTelemetry.reset();
    expandedVariationTelemetry.reset();
    variationTelemetryRevision.set(0);
    activeStepProperty.set(StepProperty::NOTE);

    stepEdit.reset();
    stepPresetPicker.reset();
    ccLaneUi.reset();
    stepPropertyInlineSelector.reset();
    stepInlineFeedback.reset();
    patternVariationFeedback.reset();
    historyFeedback.reset();
    patternQuickControls.reset();
    contentView.reset();
    structureUi.reset();
}

}  // namespace core::state::sequencer

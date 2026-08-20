#include "state/sequencer/SequencerState.hpp"

#include <algorithm>

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
    mode = sanitizePitchEditMode(static_cast<uint8_t>(mode));
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

FLASHMEM SequencerState::SequencerState() {
    stepContentDraft.bindRevisionSignal(contentView.revision);
}
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

FLASHMEM bool SequencerState::setStepNoteAt(uint8_t step, uint8_t noteValue) {
    if (step >= MAX_STEPS) return false;
    const uint8_t clamped = SequencerPatternState::clampMidi7(noteValue);
    if (pattern.note[step] == clamped) return false;
    // Runtime telemetry is a projection of the previous authored value.
    // Retire it before publishing the authored-data revision so the first
    // UI consumer of that revision cannot paint one stale frame.
    invalidateStepVariationTelemetry(step);
    return pattern.setStepNoteAt(step, clamped);
}

FLASHMEM bool SequencerState::setStepVelocityAt(uint8_t step, uint8_t velocityValue) {
    if (step >= MAX_STEPS) return false;
    const uint8_t clamped = SequencerPatternState::clampMidi7(velocityValue);
    if (pattern.velocity[step] == clamped) return false;
    invalidateStepVariationTelemetry(step);
    return pattern.setStepVelocityAt(step, clamped);
}

FLASHMEM bool SequencerState::setStepGateAt(uint8_t step, uint16_t gatePercent) {
    if (step >= MAX_STEPS) return false;
    const uint16_t clamped = SequencerPatternState::clampGatePercent(gatePercent);
    if (pattern.gate[step] == clamped) return false;
    invalidateStepVariationTelemetry(step);
    return pattern.setStepGateAt(step, clamped);
}

FLASHMEM bool SequencerState::setStepNudgeAt(uint8_t step, int8_t nudgeValue) {
    if (step >= MAX_STEPS) return false;
    const int8_t clamped = SequencerPatternState::clampNudge(nudgeValue);
    if (pattern.nudge[step] == clamped) return false;
    invalidateStepVariationTelemetry(step);
    return pattern.setStepNudgeAt(step, clamped);
}

FLASHMEM bool SequencerState::setStepProbabilityAt(
    uint8_t step,
    uint8_t probabilityValue
) {
    if (step >= MAX_STEPS) return false;
    const uint8_t clamped = SequencerPatternState::clampProbability(probabilityValue);
    if (pattern.probability[step] == clamped) return false;
    invalidateStepVariationTelemetry(step);
    return pattern.setStepProbabilityAt(step, clamped);
}

FLASHMEM bool SequencerState::setStepDataAt(
    uint8_t step,
    uint8_t noteValue,
    uint8_t velocityValue,
    uint16_t gatePercent
) {
    if (step >= MAX_STEPS) return false;
    return setStepDataAt(
        step,
        noteValue,
        velocityValue,
        gatePercent,
        pattern.nudge[step],
        pattern.probability[step]
    );
}

FLASHMEM bool SequencerState::setStepDataAt(
    uint8_t step,
    uint8_t noteValue,
    uint8_t velocityValue,
    uint16_t gatePercent,
    int8_t nudgeValue
) {
    if (step >= MAX_STEPS) return false;
    return setStepDataAt(
        step,
        noteValue,
        velocityValue,
        gatePercent,
        nudgeValue,
        pattern.probability[step]
    );
}

FLASHMEM bool SequencerState::setStepDataAt(
    uint8_t step,
    uint8_t noteValue,
    uint8_t velocityValue,
    uint16_t gatePercent,
    int8_t nudgeValue,
    uint8_t probabilityValue
) {
    if (step >= MAX_STEPS) return false;
    const uint8_t clampedNote = SequencerPatternState::clampMidi7(noteValue);
    const uint8_t clampedVelocity = SequencerPatternState::clampMidi7(velocityValue);
    const uint16_t clampedGate = SequencerPatternState::clampGatePercent(gatePercent);
    const int8_t clampedNudge = SequencerPatternState::clampNudge(nudgeValue);
    const uint8_t clampedProbability =
        SequencerPatternState::clampProbability(probabilityValue);
    if (pattern.note[step] == clampedNote &&
        pattern.velocity[step] == clampedVelocity &&
        pattern.gate[step] == clampedGate &&
        pattern.nudge[step] == clampedNudge &&
        pattern.probability[step] == clampedProbability) {
        return false;
    }
    invalidateStepVariationTelemetry(step);
    if (!pattern.setStepDataAt(
            step,
            clampedNote,
            clampedVelocity,
            clampedGate,
            clampedNudge,
            clampedProbability
        )) {
        return false;
    }
    return true;
}

FLASHMEM void SequencerPatternState::reset() {
    playStart = 0;
    loopStart = 0;
    loopEnd = DEFAULT_LENGTH;
    oc::note::sequencer::StepSequencerState::reset();
    bumpStepDataRevision();
    variationRanges = {};
    bumpPatternVariationRevision();
    scalePolicy = SequencerPatternScalePolicy::INHERIT_PROJECT;
    scaleOverride = {};
    pitchEditMode = SequencerPitchEditMode::FOLLOW_SCALE;
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
    if (stepContentDraft.active.get()) {
        stepContentDraft.noteBlockedTransition(
            SequencerStepContentDraftBlockedTransition::RESET
        );
        return;
    }
    quickControlsDraft.reset();
    pattern.reset();
    page.set(0);
    focusedStep.set(0);
    playheadStep.set(-1);
    playheadStepTickOffset.set(0);
    playheadStepPhaseQ8.set(0);
    playheadStepTicks = 1;
    probabilityCycleRevision.set(0);
    probabilityCycleMask = {};
    probabilityCycleIndex = 0;
    lastResolvedVariation = {};
    cycleVariationTelemetry.reset();
    expandedVariationTelemetry.reset();
    runtimeDiagnostics.reset();
    variationTelemetryRevision.set(0);
    activeStepProperty.set(StepProperty::NOTE);
    stepStatePropertyActive.set(false);

    stepEdit.reset();
    contextSelector.reset();
    presetLibrary.reset();
    patternPresetPreview.reset();
    ccLaneUi.reset();
    stepPropertyInlineSelector.reset();
    stepContentSelector.reset();
    stepInlineFeedback.reset();
    patternVariationFeedback.reset();
    historyFeedback.reset();
    patternQuickControls.reset();
    patternEditor.reset();
    contentView.reset();
    stepContentDraft.resetSession();
    structureUi.reset();
    drumSequencer.reset();
}

}  // namespace core::state::sequencer

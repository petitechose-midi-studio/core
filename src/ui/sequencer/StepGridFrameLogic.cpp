#include "ui/sequencer/StepGridFrameLogic.hpp"

namespace core::ui::sequencer::grid {

namespace {

bool hasAnyVariationRange(const oc::note::sequencer::StepSequencerVariationRanges& ranges) {
    return ranges.pitchSemitones > 0 ||
           ranges.velocity > 0 ||
           ranges.gatePercent > 0 ||
           ranges.nudge > 0;
}

uint8_t variationRangeForProperty(
    const oc::note::sequencer::StepSequencerVariationRanges& ranges,
    core::state::sequencer::StepProperty property
) {
    switch (property) {
        case core::state::sequencer::StepProperty::NOTE:
            return ranges.pitchSemitones;
        case core::state::sequencer::StepProperty::VELOCITY:
            return ranges.velocity;
        case core::state::sequencer::StepProperty::GATE:
            return ranges.gatePercent;
        case core::state::sequencer::StepProperty::NUDGE:
            return ranges.nudge;
        case core::state::sequencer::StepProperty::PROBABILITY:
            return 0;
    }

    return 0;
}

oc::note::sequencer::StepSequencerResolvedVariation buildBaseVariation(
    uint8_t stepIndex,
    const TileRenderState& tile,
    const oc::note::sequencer::StepSequencerVariationRanges& ranges
) {
    oc::note::sequencer::StepSequencerResolvedVariation variation{};
    variation.stepIndex = stepIndex;
    variation.triggered = false;
    variation.base = {
        .note = tile.note,
        .velocity = tile.velocity,
        .gate = tile.gate,
        .nudge = tile.nudge,
    };
    variation.resolved = variation.base;
    variation.ranges = ranges;
    return variation;
}

oc::note::sequencer::StepSequencerResolvedVariation buildTelemetryVariation(
    const oc::note::sequencer::StepSequencerCycleVariationTelemetry& telemetry,
    uint8_t stepIndex,
    const TileRenderState& tile
) {
    auto variation = buildBaseVariation(stepIndex, tile, telemetry.ranges);
    variation.cycleIndex = telemetry.cycleIndex;
    variation.triggered = telemetry.triggeredMask.test(stepIndex);
    variation.resolved = {
        .note = telemetry.resolvedNote[stepIndex],
        .velocity = telemetry.resolvedVelocity[stepIndex],
        .gate = telemetry.resolvedGate[stepIndex],
        .nudge = telemetry.resolvedNudge[stepIndex],
    };
    variation.pitchDelta = telemetry.pitchDelta[stepIndex];
    variation.velocityDelta = telemetry.velocityDelta[stepIndex];
    variation.gateDelta = telemetry.gateDelta[stepIndex];
    variation.nudgeDelta = telemetry.nudgeDelta[stepIndex];
    return variation;
}

}  // namespace

StepGridFrameState buildStepGridFrameState(const core::state::sequencer::SequencerState& sequencer) {
    StepGridFrameState frame;

    frame.activeProperty = sequencer.activeStepProperty.get();
    frame.feedbackVisible = sequencer.stepInlineFeedback.visible.get();
    frame.feedbackTouchedMask = sequencer.stepInlineFeedback.touchedMask.get();
    frame.feedbackProperty = sequencer.stepInlineFeedback.property.get();

    const uint8_t length = sequencer.length.get();
    const uint8_t page = sequencer.visiblePage();
    const uint8_t pageStart = sequencer.pageStartStepClamped(page);
    const auto enabledMask = sequencer.enabledMask.get();
    const auto probabilityCycleMask = sequencer.probabilityCycleMask;
    const int16_t playhead = sequencer.playheadStep.get();
    const bool probabilityCycleMaskActive = playhead >= 0;
    const bool patternRangeFeedbackVisible =
        sequencer.stepPropertyInlineSelector.selecting.get() ||
        (sequencer.patternVariationFeedback.visible.get() &&
         sequencer.patternVariationFeedback.property.get() == frame.activeProperty);
    const bool activeRangeVisible =
        patternRangeFeedbackVisible &&
        variationRangeForProperty(sequencer.variationRanges, frame.activeProperty) > 0;

    for (uint8_t i = 0; i < frame.tiles.size(); ++i) {
        const uint8_t absoluteStep = static_cast<uint8_t>(pageStart + i);
        auto& tile = frame.tiles[i];
        tile.absoluteStep = absoluteStep;
        tile.inPattern = absoluteStep < length;
        tile.enabled = tile.inPattern ? enabledMask.test(absoluteStep) : false;
        tile.playing =
            tile.inPattern && (playhead >= 0) && (absoluteStep == static_cast<uint8_t>(playhead));

        if (!tile.inPattern) {
            continue;
        }

        tile.probabilityCycleActive =
            !probabilityCycleMaskActive || probabilityCycleMask.test(absoluteStep);
        tile.note = sequencer.note[absoluteStep];
        tile.velocity = sequencer.velocity[absoluteStep];
        tile.probability = sequencer.probability[absoluteStep];
        tile.gate = sequencer.gate[absoluteStep];
        tile.nudge = sequencer.nudge[absoluteStep];

        const auto& telemetry = sequencer.cycleVariationTelemetry;
        const bool hasRuntimeVariation =
            tile.enabled &&
            telemetry.validMask.test(absoluteStep) &&
            telemetry.triggeredMask.test(absoluteStep) &&
            hasAnyVariationRange(telemetry.ranges);

        if (hasRuntimeVariation || (tile.enabled && activeRangeVisible)) {
            tile.variation.visible = true;
            tile.variation.rangeVisible = tile.enabled && activeRangeVisible;
            tile.variation.deltaVisible = hasRuntimeVariation;
            tile.variation.rangeProperty = frame.activeProperty;
            if (telemetry.validMask.test(absoluteStep)) {
                tile.variation.resolved = buildTelemetryVariation(telemetry, absoluteStep, tile);
            } else {
                tile.variation.resolved =
                    buildBaseVariation(absoluteStep, tile, sequencer.variationRanges);
            }
        }
    }

    return frame;
}

}  // namespace core::ui::sequencer::grid

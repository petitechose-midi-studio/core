#include "ui/sequencer/StepGridFrameLogic.hpp"

#include <config/PlatformCompat.hpp>

#include "ui/sequencer/StepContentBadgeProjection.hpp"

namespace core::ui::sequencer::grid {

namespace {

FLASHMEM bool hasAnyVariationRange(const oc::note::sequencer::StepSequencerVariationRanges& ranges) {
    return ranges.pitchSemitones > 0 ||
           ranges.velocity > 0 ||
           ranges.gatePercent > 0 ||
           ranges.nudge > 0;
}

FLASHMEM uint8_t variationRangeForProperty(
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

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildBaseVariation(
    uint8_t stepIndex,
    const TileRenderState& tile,
    const oc::note::sequencer::StepSequencerVariationRanges& ranges,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
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
    variation.scaleSettings = scaleSettings;
    variation.scale = oc::note::sequencer::resolveScaleNote(tile.note, scaleSettings);
    variation.resolved.note = variation.scale.outputNote;
    return variation;
}

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildTelemetryVariation(
    const oc::note::sequencer::StepSequencerCycleVariationTelemetry& telemetry,
    uint8_t stepIndex,
    const TileRenderState& tile
) {
    auto variation = buildBaseVariation(stepIndex, tile, telemetry.ranges, telemetry.scaleSettings);
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
    variation.scaleSettings = telemetry.scaleSettings;
    variation.scale = oc::note::sequencer::resolveScaleNote(variation.resolved.note, telemetry.scaleSettings);
    variation.scale.outputNote = variation.resolved.note;
    variation.scale.inputInScale = telemetry.scaleInMask.test(stepIndex);
    variation.scale.constrained = telemetry.scaleConstrainedMask.test(stepIndex);
    return variation;
}

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildPreviewVariation(
    uint8_t stepIndex,
    const TileRenderState& tile,
    const oc::note::sequencer::StepSequencerVariationRanges& ranges,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    return oc::note::sequencer::resolveStepVariation(
        oc::note::sequencer::StepSequencerStepValues{
            .note = tile.note,
            .velocity = tile.velocity,
            .gate = tile.gate,
            .nudge = tile.nudge,
        },
        ranges,
        scaleSettings,
        core::state::sequencer::SequencerState::MAX_GATE_PERCENT,
        0U,
        0U,
        stepIndex,
        true
    );
}

FLASHMEM bool scaleFeedbackRelevant(oc::note::sequencer::StepSequencerScaleSettings scaleSettings) {
    scaleSettings.clamp();
    return scaleSettings.type != oc::note::sequencer::StepSequencerScaleType::Chromatic;
}

}  // namespace

FLASHMEM StepGridFrameState buildStepGridFrameState(
    const core::state::sequencer::SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings
) {
    StepGridFrameState frame;
    projectScaleSettings.clamp();
    const auto effectiveScaleSettings = core::state::sequencer::resolveEffectiveScaleSettings(
        projectScaleSettings,
        sequencer.pattern.scalePolicy,
        sequencer.pattern.scaleOverride
    );

    frame.activeProperty = sequencer.activeStepProperty.get();
    frame.feedbackVisible = sequencer.stepInlineFeedback.visible.get();
    frame.feedbackTouchedMask = sequencer.stepInlineFeedback.touchedMask.get();
    frame.feedbackProperty = sequencer.stepInlineFeedback.property.get();

    const uint8_t length = sequencer.pattern.length.get();
    const uint8_t page = sequencer.visiblePage();
    const uint8_t pageStart = sequencer.pageStartStepClamped(page);
    const auto enabledMask = sequencer.pattern.enabledMask.get();
    const auto probabilityCycleMask = sequencer.probabilityCycleMask;
    const int16_t playhead = sequencer.playheadStep.get();
    const bool probabilityCycleMaskActive = playhead >= 0;
    const bool patternRangeFeedbackVisible =
        sequencer.stepPropertyInlineSelector.selecting.get() ||
        (sequencer.patternVariationFeedback.visible.get() &&
         sequencer.patternVariationFeedback.property.get() == frame.activeProperty);
    const bool patternHasVariationRanges = hasAnyVariationRange(sequencer.pattern.variationRanges);
    const bool effectiveScaleFeedbackRelevant = scaleFeedbackRelevant(effectiveScaleSettings);
    const bool activeRangeVisible =
        patternRangeFeedbackVisible &&
        variationRangeForProperty(sequencer.pattern.variationRanges, frame.activeProperty) > 0;
    const auto& telemetry = sequencer.cycleVariationTelemetry;
    const bool telemetryFeedbackRelevant =
        hasAnyVariationRange(telemetry.ranges) ||
        scaleFeedbackRelevant(telemetry.scaleSettings);

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
        tile.note = sequencer.pattern.note[absoluteStep];
        tile.velocity = sequencer.pattern.velocity[absoluteStep];
        tile.probability = sequencer.pattern.probability[absoluteStep];
        tile.gate = sequencer.pattern.gate[absoluteStep];
        tile.nudge = sequencer.pattern.nudge[absoluteStep];
        tile.contentBadges = buildStepContentBadgeProjection(sequencer.pattern, absoluteStep);

        const bool hasRuntimeVariation =
            tile.enabled &&
            telemetry.validMask.test(absoluteStep) &&
            telemetry.triggeredMask.test(absoluteStep) &&
            telemetryFeedbackRelevant;
        const bool hasPreviewFeedback =
            tile.enabled &&
            (patternHasVariationRanges || effectiveScaleFeedbackRelevant);
        const bool stepInlineEditActive =
            frame.feedbackVisible && frame.feedbackTouchedMask.test(absoluteStep);

        if (hasRuntimeVariation || hasPreviewFeedback || (tile.enabled && activeRangeVisible)) {
            tile.variation.visible = true;
            tile.variation.rangeVisible = tile.enabled && activeRangeVisible;
            tile.variation.deltaVisible = hasRuntimeVariation || hasPreviewFeedback;
            tile.variation.rangeProperty = frame.activeProperty;
            if (telemetry.validMask.test(absoluteStep) && !stepInlineEditActive) {
                tile.variation.resolved = buildTelemetryVariation(telemetry, absoluteStep, tile);
            } else if (hasPreviewFeedback) {
                tile.variation.resolved = buildPreviewVariation(
                    absoluteStep,
                    tile,
                    sequencer.pattern.variationRanges,
                    effectiveScaleSettings
                );
            } else {
                tile.variation.resolved =
                    buildBaseVariation(
                        absoluteStep,
                        tile,
                        sequencer.pattern.variationRanges,
                        effectiveScaleSettings
                    );
            }
        }
    }

    return frame;
}

}  // namespace core::ui::sequencer::grid

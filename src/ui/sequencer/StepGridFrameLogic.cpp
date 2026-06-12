#include "ui/sequencer/StepGridFrameLogic.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
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

FLASHMEM bool firstChildSummary(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerContentStepProjection& projection,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    core::state::sequencer::SequencerChildContentSummary& outSummary
) {
    if (!core::state::sequencer::stepContentProjectionHasAnyChild(projection)) {
        return false;
    }

    return core::state::sequencer::resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        scaleSettings,
        outSummary
    );
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

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildChildContentVariation(
    uint8_t stepIndex,
    const core::state::sequencer::SequencerContentStepProjection& projection,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    oc::note::sequencer::StepSequencerResolvedVariation variation{};
    variation.stepIndex = stepIndex;
    variation.triggered = projection.enabled;
    variation.base = {
        .note = projection.parentNote,
        .velocity = projection.parentVelocity,
        .gate = projection.parentGate,
        .nudge = projection.parentNudge,
    };
    variation.resolved = {
        .note = projection.note,
        .velocity = projection.velocity,
        .gate = projection.gate,
        .nudge = projection.nudge,
    };
    variation.scaleSettings = scaleSettings;
    variation.scale = oc::note::sequencer::resolveScaleNote(projection.note, scaleSettings);
    variation.scale.outputNote = projection.note;
    variation.pitchDelta = static_cast<int8_t>(
        std::clamp<int>(projection.noteOffset, -128, 127)
    );
    variation.velocityDelta = static_cast<int8_t>(
        std::clamp<int>(projection.velocityOffset, -128, 127)
    );
    variation.gateDelta = static_cast<int8_t>(
        std::clamp<int>(projection.gateOffset, -128, 127)
    );
    variation.nudgeDelta = static_cast<int8_t>(
        std::clamp<int>(projection.nudgeOffset, -128, 127)
    );
    return variation;
}

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildChildSummaryVariation(
    uint8_t stepIndex,
    const core::state::sequencer::SequencerContentStepProjection& projection,
    const core::state::sequencer::SequencerChildContentSummary& summary,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    oc::note::sequencer::StepSequencerResolvedVariation variation{};
    variation.stepIndex = stepIndex;
    variation.triggered = summary.enabled;
    variation.base = {
        .note = projection.note,
        .velocity = projection.velocity,
        .gate = projection.gate,
        .nudge = projection.nudge,
    };
    variation.resolved = {
        .note = summary.note,
        .velocity = summary.velocity,
        .gate = summary.gate,
        .nudge = summary.nudge,
    };
    variation.scaleSettings = scaleSettings;
    variation.scale = oc::note::sequencer::resolveScaleNote(summary.note, scaleSettings);
    variation.scale.outputNote = summary.note;
    variation.pitchDelta = static_cast<int8_t>(
        std::clamp<int>(static_cast<int>(summary.note) - static_cast<int>(projection.note), -128, 127)
    );
    variation.velocityDelta = static_cast<int8_t>(
        std::clamp<int>(
            static_cast<int>(summary.velocity) - static_cast<int>(projection.velocity),
            -128,
            127
        )
    );
    variation.gateDelta = static_cast<int8_t>(
        std::clamp<int>(
            static_cast<int>(summary.gate) - static_cast<int>(projection.gate),
            -128,
            127
        )
    );
    variation.nudgeDelta = static_cast<int8_t>(
        std::clamp<int>(
            static_cast<int>(summary.nudge) - static_cast<int>(projection.nudge),
            -128,
            127
        )
    );
    return variation;
}

FLASHMEM bool childSummaryDiffersFromProjection(
    const core::state::sequencer::SequencerContentStepProjection& projection,
    const core::state::sequencer::SequencerChildContentSummary& summary
) {
    return summary.enabled != projection.enabled ||
           summary.note != projection.note ||
           summary.velocity != projection.velocity ||
           summary.gate != projection.gate ||
           summary.nudge != projection.nudge ||
           summary.probability != projection.probability;
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

    const bool childContext = core::state::sequencer::isChildContentView(sequencer);
    const uint8_t length = core::state::sequencer::activeContentLength(sequencer);
    const uint8_t page = core::state::sequencer::normalizeActiveContentPage(
        sequencer,
        sequencer.page.get()
    );
    const uint8_t pageStart = core::state::sequencer::activeContentPageStartStep(sequencer, page);
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
    const auto contentPlayback = childContext
        ? core::state::sequencer::resolveActiveContentPlaybackProjection(
              sequencer,
              effectiveScaleSettings
          )
        : core::state::sequencer::SequencerContentPlaybackProjection{};

    for (uint8_t i = 0; i < frame.tiles.size(); ++i) {
        const uint8_t absoluteStep = static_cast<uint8_t>(pageStart + i);
        auto& tile = frame.tiles[i];
        tile.absoluteStep = absoluteStep;
        tile.inPattern = absoluteStep < length;
        tile.enabled = tile.inPattern ? enabledMask.test(absoluteStep) : false;
        tile.playheadVisible =
            tile.inPattern &&
            (playhead >= 0) &&
            (childContext
                 ? (contentPlayback.visible && absoluteStep == contentPlayback.step)
                 : (absoluteStep == static_cast<uint8_t>(playhead)));
        tile.playing =
            tile.playheadVisible &&
            (childContext ? contentPlayback.active : true);

        if (!tile.inPattern) {
            continue;
        }

        tile.probabilityCycleActive =
            childContext || !probabilityCycleMaskActive || probabilityCycleMask.test(absoluteStep);
        const auto projection = core::state::sequencer::resolveActiveContentStepProjection(
            sequencer,
            absoluteStep,
            effectiveScaleSettings
        );
        if (!projection.valid) {
            tile.inPattern = false;
            tile.enabled = false;
            continue;
        }
        tile.enabled = projection.enabled;
        tile.note = projection.note;
        tile.velocity = projection.velocity;
        tile.probability = projection.probability;
        tile.gate = projection.gate;
        tile.nudge = projection.nudge;
        tile.childContentContext = childContext;
        tile.childContentOffset = childContext
            ? core::state::sequencer::stepContentProjectionOffsetForProperty(
                  projection,
                  frame.activeProperty
              )
            : 0;
        tile.childContentNoteOffsetUsesScaleDegrees =
            childContext &&
            frame.activeProperty == core::state::sequencer::StepProperty::NOTE &&
            effectiveScaleSettings.isConstrained();
        tile.contentBadges = buildStepContentBadgeProjectionForNode(
            sequencer.pattern,
            projection.nodeId
        );
        core::state::sequencer::SequencerChildContentSummary childSummary{};
        const bool childSummaryTouched = firstChildSummary(
                sequencer,
                projection,
                effectiveScaleSettings,
                childSummary
            );
        const bool childSummaryChanged =
            childSummaryTouched && childSummaryDiffersFromProjection(projection, childSummary);
        if (childSummaryTouched) {
            tile.probabilityCycleActive =
                tile.probabilityCycleActive && childSummary.enabled;
            if (childSummary.note != projection.note) {
                tile.childPitchSummaryVisible = true;
                tile.childPitchSummaryNote = childSummary.note;
            }
            tile.velocity = childSummary.velocity;
            tile.probability = childSummary.probability;
            tile.gate = childSummary.gate;
            tile.nudge = childSummary.nudge;
        }

        if (!tile.enabled || !tile.probabilityCycleActive) {
            tile.playing = false;
        }

        const bool hasRuntimeVariation =
            !childContext &&
            tile.enabled &&
            telemetry.validMask.test(absoluteStep) &&
            telemetry.triggeredMask.test(absoluteStep) &&
            telemetryFeedbackRelevant;
        const bool hasPreviewFeedback =
            tile.enabled &&
            (patternHasVariationRanges || effectiveScaleFeedbackRelevant);
        const bool stepInlineEditActive =
            frame.feedbackVisible && frame.feedbackTouchedMask.test(absoluteStep);

        if (childSummaryChanged) {
            tile.variation.visible = true;
            tile.variation.rangeVisible = false;
            tile.variation.deltaVisible = true;
            tile.variation.rangeProperty = frame.activeProperty;
            tile.variation.resolved = buildChildSummaryVariation(
                absoluteStep,
                projection,
                childSummary,
                effectiveScaleSettings
            );
        } else if (childContext && tile.enabled) {
            tile.variation.visible = true;
            tile.variation.rangeVisible = false;
            tile.variation.deltaVisible = true;
            tile.variation.rangeProperty = frame.activeProperty;
            tile.variation.resolved = buildChildContentVariation(
                absoluteStep,
                projection,
                effectiveScaleSettings
            );
        } else if (hasRuntimeVariation || hasPreviewFeedback || (tile.enabled && activeRangeVisible)) {
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

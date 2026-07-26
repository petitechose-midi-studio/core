#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::state::sequencer {

namespace {

FLASHMEM oc::note::sequencer::StepSequencerVariationRanges combineVariationRanges(
    oc::note::sequencer::StepSequencerVariationRanges global,
    oc::note::sequencer::StepSequencerVariationRanges local
) {
    using Ranges = oc::note::sequencer::StepSequencerVariationRanges;

    global.clamp();
    local.clamp();
    return {
        .pitchSemitones = static_cast<uint8_t>(
            std::min<uint16_t>(
                static_cast<uint16_t>(global.pitchSemitones) + local.pitchSemitones,
                Ranges::MAX_PITCH_SEMITONES
            )
        ),
        .velocity = static_cast<uint8_t>(
            std::min<uint16_t>(
                static_cast<uint16_t>(global.velocity) + local.velocity,
                Ranges::MAX_VELOCITY
            )
        ),
        .gatePercent = static_cast<uint8_t>(
            std::min<uint16_t>(
                static_cast<uint16_t>(global.gatePercent) + local.gatePercent,
                Ranges::MAX_GATE_PERCENT
            )
        ),
        .nudge = static_cast<uint8_t>(
            std::min<uint16_t>(
                static_cast<uint16_t>(global.nudge) + local.nudge,
                Ranges::MAX_NUDGE
            )
        ),
    };
}

FLASHMEM oc::note::sequencer::StepSequencerVariationRanges localVariationForNode(
    const oc::note::sequencer::StepSequencerGraph* graph,
    SequencerGraphNodeId nodeId
) {
    if (graph == nullptr) return {};
    const auto* node = graph->stepNode(nodeId);
    if (node == nullptr) return {};
    auto ranges = node->localVariation;
    ranges.clamp();
    return ranges;
}

FLASHMEM oc::note::sequencer::StepSequencerVariationRanges inheritedLocalVariationForContentPath(
    const SequencerState& sequencer,
    const oc::note::sequencer::StepSequencerGraph* graph
) {
    oc::note::sequencer::StepSequencerVariationRanges ranges{};
    if (graph == nullptr || !isChildContentView(sequencer)) {
        return ranges;
    }

    const auto& view = sequencer.contentView;
    const uint8_t depth = std::min<uint8_t>(
        view.stackDepth,
        static_cast<uint8_t>(view.frames.size())
    );
    for (uint8_t i = 0; i < depth; ++i) {
        ranges = combineVariationRanges(
            ranges,
            localVariationForNode(graph, view.frames[i].ownerNodeId)
        );
    }
    return ranges;
}

FLASHMEM bool scaleFeedbackRelevant(
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    scaleSettings.clamp();
    return scaleSettings.type != oc::note::sequencer::StepSequencerScaleType::Chromatic;
}

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildBaseVariation(
    uint8_t stepIndex,
    oc::note::sequencer::StepSequencerStepValues values,
    const oc::note::sequencer::StepSequencerVariationRanges& ranges,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    oc::note::sequencer::StepSequencerResolvedVariation variation{};
    variation.stepIndex = stepIndex;
    variation.triggered = false;
    variation.base = values;
    variation.resolved = variation.base;
    variation.ranges = ranges;
    variation.scaleSettings = scaleSettings;
    variation.scale = oc::note::sequencer::resolveScaleNote(values.note, scaleSettings);
    variation.resolved.note = variation.scale.outputNote;
    return variation;
}

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildBaseVariation(
    uint8_t stepIndex,
    const SequencerResolvedStepDisplayState& step,
    const oc::note::sequencer::StepSequencerVariationRanges& ranges,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    return buildBaseVariation(
        stepIndex,
        oc::note::sequencer::StepSequencerStepValues{
            .note = step.note,
            .velocity = step.velocity,
            .gate = step.gate,
            .nudge = step.nudge,
        },
        ranges,
        scaleSettings
    );
}

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildTelemetryVariation(
    const oc::note::sequencer::StepSequencerCycleVariationTelemetry& telemetry,
    uint8_t stepIndex,
    const SequencerResolvedStepDisplayState& step
) {
    auto variation = buildBaseVariation(stepIndex, step, telemetry.ranges, telemetry.scaleSettings);
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
    variation.scale = oc::note::sequencer::resolveScaleNote(
        variation.resolved.note,
        telemetry.scaleSettings
    );
    variation.scale.outputNote = variation.resolved.note;
    variation.scale.inputInScale = telemetry.scaleInMask.test(stepIndex);
    variation.scale.constrained = telemetry.scaleConstrainedMask.test(stepIndex);
    return variation;
}

FLASHMEM bool expandedTelemetryVariationForNode(
    const SequencerState& sequencer,
    SequencerGraphNodeId nodeId,
    oc::note::sequencer::StepSequencerResolvedVariation& outVariation
) {
    const auto& telemetry = sequencer.expandedVariationTelemetry;
    if (!telemetry.valid ||
        nodeId == SequencerContentStepProjection::INVALID_ID ||
        sequencer.playheadStep.get() < 0 ||
        telemetry.rootStepIndex != static_cast<uint8_t>(sequencer.playheadStep.get())) {
        return false;
    }

    const uint32_t offset = sequencer.playheadStepTickOffset.get();
    for (uint8_t i = 0; i < telemetry.count; ++i) {
        if (telemetry.nodeId[i] != nodeId) continue;
        const uint32_t start = telemetry.localTick[i];
        const uint32_t end = start + static_cast<uint32_t>(telemetry.spanTicks[i]);
        if (offset >= start && offset < end) {
            outVariation = telemetry.variation[i];
            return true;
        }
    }
    return false;
}

FLASHMEM bool expandedTelemetryVariationAtCurrentOffset(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerResolvedVariation& outVariation
) {
    const auto& telemetry = sequencer.expandedVariationTelemetry;
    if (!telemetry.valid || sequencer.playheadStep.get() < 0) {
        return false;
    }
    if (telemetry.rootStepIndex != static_cast<uint8_t>(sequencer.playheadStep.get())) {
        return false;
    }

    const uint32_t offset = sequencer.playheadStepTickOffset.get();
    for (uint8_t i = 0; i < telemetry.count; ++i) {
        const uint32_t start = telemetry.localTick[i];
        const uint32_t span = telemetry.spanTicks[i] == 0 ? 1U : telemetry.spanTicks[i];
        const uint32_t end = start + span;
        if (offset >= start && offset < end) {
            outVariation = telemetry.variation[i];
            return true;
        }
    }
    return false;
}

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildPreviewVariation(
    uint8_t stepIndex,
    uint32_t stepIdentity,
    oc::note::sequencer::StepSequencerStepValues values,
    const oc::note::sequencer::StepSequencerVariationRanges& ranges,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    return oc::note::sequencer::resolveStepVariation(
        values,
        ranges,
        scaleSettings,
        SequencerState::MAX_GATE_PERCENT,
        0U,
        0U,
        stepIndex,
        true,
        stepIdentity
    );
}

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildPreviewVariation(
    uint8_t stepIndex,
    uint32_t stepIdentity,
    const SequencerResolvedStepDisplayState& step,
    const oc::note::sequencer::StepSequencerVariationRanges& ranges,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings
) {
    return buildPreviewVariation(
        stepIndex,
        stepIdentity,
        oc::note::sequencer::StepSequencerStepValues{
            .note = step.note,
            .velocity = step.velocity,
            .gate = step.gate,
            .nudge = step.nudge,
        },
        ranges,
        scaleSettings
    );
}

FLASHMEM oc::note::sequencer::StepSequencerResolvedVariation buildChildContentVariation(
    uint8_t stepIndex,
    const SequencerContentStepProjection& projection,
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
    const SequencerContentStepProjection& projection,
    const SequencerChildContentSummary& summary,
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
    const SequencerContentStepProjection& projection,
    const SequencerChildContentSummary& summary
) {
    return summary.enabled != projection.enabled ||
           summary.note != projection.note ||
           summary.velocity != projection.velocity ||
           summary.gate != projection.gate ||
           summary.nudge != projection.nudge ||
           summary.probability != projection.probability;
}

FLASHMEM bool firstChildSummary(
    const SequencerState& sequencer,
    const SequencerContentStepProjection& projection,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    SequencerChildContentSummary& outSummary
) {
    if (!stepContentProjectionHasAnyChild(projection)) {
        return false;
    }

    return resolveRepresentativeChildContentSummary(
        sequencer,
        projection,
        scaleSettings,
        outSummary
    );
}

FLASHMEM bool resolvedDisplayProjectionHasAnyVariationRange(
    const oc::note::sequencer::StepSequencerVariationRanges& ranges
) {
    return ranges.pitchSemitones > 0 ||
           ranges.velocity > 0 ||
           ranges.gatePercent > 0 ||
           ranges.nudge > 0;
}

FLASHMEM uint8_t resolvedDisplayVariationRangeForProperty(
    const oc::note::sequencer::StepSequencerVariationRanges& ranges,
    StepProperty property
) {
    switch (property) {
        case StepProperty::NOTE:
            return ranges.pitchSemitones;
        case StepProperty::VELOCITY:
            return ranges.velocity;
        case StepProperty::GATE:
            return ranges.gatePercent;
        case StepProperty::NUDGE:
            return ranges.nudge;
        case StepProperty::PROBABILITY:
            return 0;
    }

    return 0;
}

}  // namespace

FLASHMEM SequencerResolvedDisplayProjectionContext makeSequencerResolvedDisplayProjectionContext(
    const SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings,
    StepProperty activeProperty
) {
    projectScaleSettings.clamp();
    const auto& pattern = authoringPattern(sequencer);
    const auto effectiveScaleSettings = resolveEffectiveScaleSettings(
        projectScaleSettings,
        pattern.scalePolicy,
        pattern.scaleOverride
    );
    const auto& telemetry = sequencer.cycleVariationTelemetry;
    const auto* graph = graphView(pattern);

    SequencerResolvedDisplayProjectionContext context{};
    context.sequencer = &sequencer;
    context.scaleSettings = effectiveScaleSettings;
    context.activeProperty = activeProperty;
    context.childContext = isChildContentView(sequencer);
    context.length = activeContentLength(sequencer);
    context.probabilityCycleMaskActive = sequencer.playheadStep.get() >= 0;
    context.effectiveScaleFeedbackRelevant = scaleFeedbackRelevant(effectiveScaleSettings);
    context.telemetryFeedbackRelevant =
        resolvedDisplayProjectionHasAnyVariationRange(telemetry.ranges) ||
        scaleFeedbackRelevant(telemetry.scaleSettings);
    context.contentPlayback = context.childContext
        ? resolveActiveContentPlaybackProjection(sequencer, effectiveScaleSettings)
        : SequencerContentPlaybackProjection{};
    context.graph = graph;
    context.inheritedLocalVariation = inheritedLocalVariationForContentPath(sequencer, graph);
    return context;
}

FLASHMEM SequencerResolvedStepDisplayState buildSequencerResolvedStepDisplayState(
    const SequencerResolvedDisplayProjectionContext& context,
    uint8_t absoluteStep,
    bool authoringProjectionActive
) {
    SequencerResolvedStepDisplayState step{};
    if (context.sequencer == nullptr) return step;

    const auto& sequencer = *context.sequencer;
    step.inPattern = absoluteStep < context.length;
    if (!step.inPattern) return step;

    const int16_t playhead = sequencer.playheadStep.get();
    step.playheadVisible =
        (playhead >= 0) &&
        (context.childContext
             ? (context.contentPlayback.visible && absoluteStep == context.contentPlayback.step)
             : (absoluteStep == static_cast<uint8_t>(playhead)));
    step.playing =
        step.playheadVisible &&
        (context.childContext ? context.contentPlayback.active : true);

    step.probabilityCycleActive =
        context.childContext ||
        !context.probabilityCycleMaskActive ||
        sequencer.probabilityCycleMask.test(absoluteStep);

    const auto projection = resolveActiveContentStepProjection(
        sequencer,
        absoluteStep,
        context.scaleSettings
    );
    if (!projection.valid) {
        step.inPattern = false;
        return step;
    }

    step.valid = true;
    step.enabled = projection.enabled;
    step.note = projection.note;
    step.velocity = projection.velocity;
    step.probability = projection.probability;
    step.gate = projection.gate;
    step.nudge = projection.nudge;
    step.childContentContext = context.childContext;
    step.childContentOffset = context.childContext
        ? stepContentProjectionOffsetForProperty(projection, context.activeProperty)
        : 0;
    step.childContentNoteOffsetUsesScaleDegrees =
        context.childContext &&
        context.activeProperty == StepProperty::NOTE &&
        context.scaleSettings.isConstrained();
    step.nodeId = projection.nodeId;
    step.runtimeNodeId = projection.nodeId;

    const auto localVariation = combineVariationRanges(
        context.inheritedLocalVariation,
        localVariationForNode(context.graph, projection.nodeId)
    );
    const auto effectiveVariationRanges = combineVariationRanges(
        authoringPattern(sequencer).variationRanges,
        localVariation
    );
    const bool effectiveHasVariationRanges =
        resolvedDisplayProjectionHasAnyVariationRange(effectiveVariationRanges);
    const bool activeRangeVisible =
        resolvedDisplayVariationRangeForProperty(
            effectiveVariationRanges,
            context.activeProperty
        ) > 0;

    SequencerChildContentSummary childSummary{};
    const bool childSummaryTouched = firstChildSummary(
        sequencer,
        projection,
        context.scaleSettings,
        childSummary
    );
    const bool childSummaryChanged =
        childSummaryTouched && childSummaryDiffersFromProjection(projection, childSummary);
    const auto childSummaryEffectiveVariationRanges = childSummaryTouched
        ? combineVariationRanges(effectiveVariationRanges, childSummary.localVariation)
        : effectiveVariationRanges;
    const bool childSummaryHasVariationRanges =
        childSummaryTouched &&
        resolvedDisplayProjectionHasAnyVariationRange(childSummaryEffectiveVariationRanges);
    const bool childSummaryActiveRangeVisible =
        childSummaryTouched &&
        resolvedDisplayVariationRangeForProperty(
            childSummaryEffectiveVariationRanges,
            context.activeProperty
        ) > 0;
    const bool childSummaryPreviewRelevant =
        childSummaryTouched &&
        step.enabled &&
        (childSummaryHasVariationRanges || context.effectiveScaleFeedbackRelevant);

    oc::note::sequencer::StepSequencerResolvedVariation expandedRuntimeVariation{};
    step.runtimeNodeId = childSummaryTouched ? childSummary.nodeId : projection.nodeId;
    bool hasExpandedRuntimeVariation = expandedTelemetryVariationForNode(
        sequencer,
        step.runtimeNodeId,
        expandedRuntimeVariation
    );
    if (!hasExpandedRuntimeVariation && step.playheadVisible) {
        hasExpandedRuntimeVariation = expandedTelemetryVariationAtCurrentOffset(
            sequencer,
            expandedRuntimeVariation
        );
    }

    if (childSummaryTouched) {
        step.probabilityCycleActive =
            step.probabilityCycleActive && childSummary.enabled;
        if (childSummary.note != projection.note) {
            step.childPitchSummaryVisible = true;
            step.childPitchSummaryNote = childSummary.note;
        }
        step.velocity = childSummary.velocity;
        step.probability = childSummary.probability;
        step.gate = childSummary.gate;
        step.nudge = childSummary.nudge;
    }

    if (!step.enabled || !step.probabilityCycleActive) {
        step.playing = false;
    }

    const auto& telemetry = sequencer.cycleVariationTelemetry;
    const bool hasRuntimeVariation =
        !context.childContext &&
        step.enabled &&
        telemetry.validMask.test(absoluteStep) &&
        telemetry.triggeredMask.test(absoluteStep) &&
        context.telemetryFeedbackRelevant;
    const bool hasPreviewFeedback =
        step.enabled &&
        (effectiveHasVariationRanges || context.effectiveScaleFeedbackRelevant);

    step.variation.rangeProperty = context.activeProperty;
    if (hasExpandedRuntimeVariation && !authoringProjectionActive) {
        if (step.playheadVisible) {
            step.probabilityCycleActive = true;
            step.playing = step.enabled;
        }
        step.variation.visible = true;
        step.variation.rangeVisible = childSummaryTouched
            ? childSummaryActiveRangeVisible
            : activeRangeVisible;
        step.variation.deltaVisible = true;
        step.variation.resolved = expandedRuntimeVariation;
    } else if (childSummaryChanged || childSummaryPreviewRelevant) {
        step.variation.visible = true;
        step.variation.rangeVisible =
            childSummaryPreviewRelevant && childSummaryActiveRangeVisible;
        step.variation.deltaVisible = true;
        if (childSummaryPreviewRelevant) {
            const uint32_t summaryIdentity =
                childSummary.nodeId != SequencerContentStepProjection::INVALID_ID
                    ? childSummary.nodeId
                    : projection.nodeId;
            step.variation.resolved = buildPreviewVariation(
                absoluteStep,
                summaryIdentity,
                oc::note::sequencer::StepSequencerStepValues{
                    .note = childSummary.note,
                    .velocity = childSummary.velocity,
                    .gate = childSummary.gate,
                    .nudge = childSummary.nudge,
                },
                childSummaryEffectiveVariationRanges,
                context.scaleSettings
            );
        } else {
            step.variation.resolved = buildChildSummaryVariation(
                absoluteStep,
                projection,
                childSummary,
                context.scaleSettings
            );
        }
    } else if (context.childContext && step.enabled) {
        step.variation.visible = true;
        step.variation.rangeVisible = false;
        step.variation.deltaVisible = true;
        if (hasPreviewFeedback) {
            step.variation.rangeVisible = activeRangeVisible;
            step.variation.resolved = buildPreviewVariation(
                absoluteStep,
                projection.nodeId,
                step,
                effectiveVariationRanges,
                context.scaleSettings
            );
        } else {
            step.variation.resolved = buildChildContentVariation(
                absoluteStep,
                projection,
                context.scaleSettings
            );
        }
    } else if (hasRuntimeVariation || hasPreviewFeedback || (step.enabled && activeRangeVisible)) {
        step.variation.visible = true;
        step.variation.rangeVisible = step.enabled && activeRangeVisible;
        step.variation.deltaVisible = hasRuntimeVariation || hasPreviewFeedback;
        if (hasRuntimeVariation && !authoringProjectionActive) {
            step.variation.resolved = buildTelemetryVariation(telemetry, absoluteStep, step);
        } else if (hasPreviewFeedback) {
            step.variation.resolved = buildPreviewVariation(
                absoluteStep,
                projection.nodeId,
                step,
                effectiveVariationRanges,
                context.scaleSettings
            );
        } else {
            step.variation.resolved = buildBaseVariation(
                absoluteStep,
                step,
                effectiveVariationRanges,
                context.scaleSettings
            );
        }
    }

    return step;
}

FLASHMEM SequencerResolvedStepDisplayState buildSequencerStepEditorDisplayState(
    const SequencerResolvedDisplayProjectionContext& context,
    uint8_t absoluteStep
) {
    return buildSequencerResolvedStepDisplayState(
        context,
        absoluteStep,
        true
    );
}

FLASHMEM oc::note::sequencer::StepSequencerStepValues sequencerResolvedStepDisplayValues(
    const SequencerResolvedStepDisplayState& step
) {
    if (step.variation.visible) {
        return step.variation.resolved.resolved;
    }
    return {
        .note = step.note,
        .velocity = step.velocity,
        .gate = step.gate,
        .nudge = step.nudge,
    };
}

}  // namespace core::state::sequencer

#include "sequencer/SequencerRuntimeStateSync.hpp"

#include <algorithm>

#include <oc/diagnostics/Performance.hpp>

namespace core::sequencer {
namespace {

bool expandedTelemetryNeedsIntraStepOffset(
    const oc::note::sequencer::StepSequencerExpandedVariationTelemetry& telemetry,
    uint16_t playheadStepTicks
) {
    if (!telemetry.valid || telemetry.count == 0) return false;

    const uint16_t stepTicks = playheadStepTicks == 0 ? 1 : playheadStepTicks;
    if (telemetry.count > 1) return true;

    for (uint8_t i = 0; i < telemetry.count; ++i) {
        if (telemetry.localTick[i] != 0) return true;
        if (telemetry.spanTicks[i] < stepTicks) return true;
    }
    return false;
}

uint16_t projectedExpandedTelemetryOffset(
    const oc::note::sequencer::StepSequencerExpandedVariationTelemetry& telemetry,
    uint16_t playheadStepTickOffset
) {
    uint32_t selectedOffset = 0;
    const uint32_t currentOffset = playheadStepTickOffset;
    for (uint8_t i = 0; i < telemetry.count; ++i) {
        const uint32_t start = telemetry.localTick[i];
        const uint32_t span = telemetry.spanTicks[i] == 0 ? 1U : telemetry.spanTicks[i];
        const uint32_t end = start + span;
        if (currentOffset >= start && currentOffset < end) {
            return static_cast<uint16_t>(std::min<uint32_t>(start, UINT16_MAX));
        }
        if (start <= currentOffset && start >= selectedOffset) {
            selectedOffset = start;
        }
    }
    return static_cast<uint16_t>(std::min<uint32_t>(selectedOffset, UINT16_MAX));
}

void recordNewRuntimeDiagnostics(
    const oc::note::sequencer::StepSequencerRuntimeDiagnostics& previous,
    const oc::note::sequencer::StepSequencerRuntimeDiagnostics& current
) {
    if (current.noteBudgetExceededCount > previous.noteBudgetExceededCount) {
        OC_PERF_RECORD(
            "sequencer.expansion.note-budget-exceeded",
            0U,
            current.noteBudgetExceededCount - previous.noteBudgetExceededCount,
            current.noteBudgetExceededCount
        );
    }
    if (current.schedulerCapacityExceededCount >
        previous.schedulerCapacityExceededCount) {
        OC_PERF_RECORD(
            "sequencer.scheduler.capacity-exceeded",
            0U,
            current.schedulerCapacityExceededCount -
                previous.schedulerCapacityExceededCount,
            current.schedulerCapacityExceededCount
        );
    }
    if (current.depthLimitReachedCount > previous.depthLimitReachedCount) {
        OC_PERF_RECORD(
            "sequencer.expansion.depth-limit-reached",
            0U,
            current.depthLimitReachedCount - previous.depthLimitReachedCount,
            current.depthLimitReachedCount
        );
    }
}

}  // namespace

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerState& source,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings,
    ProjectTimingContext projectTiming
) {
    return captureRuntimeStateSignature(source.pattern, projectScaleSettings, projectTiming);
}

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerPatternState& source,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings,
    ProjectTimingContext projectTiming
) {
    return {
        .length = source.length.get(),
        .playStart = source.playStart,
        .loopStart = source.loopStart,
        .loopEnd = source.loopEnd,
        .stepsPerBeat = source.stepsPerBeat.get(),
        .enabledMask = source.enabledMask.get(),
        .stepDataRevision = source.stepDataRevision.get(),
        .patternVariationRevision = source.patternVariationRevision.get(),
        .patternScaleRevision = source.patternScaleRevision.get(),
        .patternTimingRevision = source.patternTimingRevision.get(),
        .graphRevision = source.graphRevision.get(),
        .effectiveSwingPercent = source.effectiveSwingPercent(projectTiming.swingPercent),
        .patternNudgePercent = source.patternNudgePercent.get(),
        .pitchFollowsScale =
            source.pitchEditMode ==
                core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE,
        .effectiveScaleSettings = core::state::sequencer::resolveEffectiveScaleSettings(
            projectScaleSettings,
            source.scalePolicy,
            source.scaleOverride
        ),
    };
}

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerPatternSnapshot& source
) {
    return {
        .length = source.length,
        .playStart = source.playStart,
        .loopStart = source.loopStart,
        .loopEnd = source.loopEnd,
        .stepsPerBeat = source.stepsPerBeat,
        .enabledMask = source.enabledMask,
        .stepDataRevision = source.stepDataRevision,
        .patternVariationRevision = source.patternVariationRevision,
        .patternScaleRevision = source.patternScaleRevision,
        .patternTimingRevision = source.patternTimingRevision,
        .graphRevision = source.graphRevision,
        .effectiveSwingPercent = source.effectiveSwingPercent,
        .patternNudgePercent = source.patternNudgePercent,
        .pitchFollowsScale =
            source.pitchEditMode ==
                core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE,
        .effectiveScaleSettings = source.effectiveScaleSettings,
    };
}

oc::note::sequencer::StepSequencerPlaybackRegion runtimePlaybackRegion(
    const core::state::sequencer::SequencerPatternSnapshot& source
) {
    const uint8_t length = std::clamp<uint8_t>(
        source.length,
        oc::note::sequencer::StepSequencerPlaybackRegion::MIN_CONTENT_LENGTH,
        oc::note::sequencer::StepSequencerPlaybackRegion::MAX_CONTENT_LENGTH
    );
    const oc::note::sequencer::StepSequencerPlaybackRegion region{
        length,
        source.playStart,
        source.loopStart,
        source.loopEnd,
    };
    return region.isValid()
        ? region
        : oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(length);
}

void syncRuntimeState(oc::note::sequencer::StepSequencerRuntimeState& target,
                      const core::state::sequencer::SequencerPatternSnapshot& source) {
    target.length = source.length;
    target.stepsPerBeat = source.stepsPerBeat;
    target.effectiveSwingPercent = source.effectiveSwingPercent;
    target.patternNudgePercent = source.patternNudgePercent;
    target.enabledMask = source.enabledMask;
    target.scaleSettings = source.effectiveScaleSettings;
    target.scaleSettings.clamp();
    target.pitchFollowsScale =
        source.pitchEditMode ==
            core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE;
    target.variationRanges = source.variationRanges;
    target.variationRanges.clamp();

    std::copy(source.note.begin(), source.note.end(), target.note.begin());
    std::copy(source.velocity.begin(), source.velocity.end(), target.velocity.begin());
    std::copy(source.gate.begin(), source.gate.end(), target.gate.begin());
    std::copy(source.nudge.begin(), source.nudge.end(), target.nudge.begin());
    std::copy(source.probability.begin(), source.probability.end(), target.probability.begin());
}

SequencerRuntimeTelemetrySnapshot captureRuntimeTelemetry(
    const oc::note::sequencer::StepSequencerRuntimeState& runtimeState
) {
    return {
        .playheadStep = runtimeState.playheadStep,
        .playheadStepTickOffset = runtimeState.playheadStepTickOffset,
        .playheadStepTicks = runtimeState.playheadStepTicks,
        .probabilityCycleIndex = runtimeState.probabilityCycleIndex,
        .probabilityCycleMask = runtimeState.probabilityCycleMask,
        .variationTelemetryRevision = runtimeState.variationTelemetryRevision,
        .lastResolvedVariation = runtimeState.lastResolvedVariation,
        .cycleVariationTelemetry = runtimeState.cycleVariationTelemetry,
        .expandedVariationTelemetry = runtimeState.expandedVariationTelemetry,
        .runtimeDiagnostics = runtimeState.runtimeDiagnostics,
    };
}

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const SequencerRuntimeTelemetrySnapshot& telemetry) {
    const auto previousDiagnostics = target.runtimeDiagnostics;
    target.expandedVariationTelemetry = telemetry.expandedVariationTelemetry;
    target.runtimeDiagnostics = telemetry.runtimeDiagnostics;
    recordNewRuntimeDiagnostics(previousDiagnostics, target.runtimeDiagnostics);

    target.playheadStep.set(telemetry.playheadStep);
    target.playheadStepTicks = telemetry.playheadStepTicks == 0 ? 1 : telemetry.playheadStepTicks;

    const bool needsIntraStepOffset =
        target.contentView.isChildContent() ||
        expandedTelemetryNeedsIntraStepOffset(
            telemetry.expandedVariationTelemetry,
            target.playheadStepTicks
        );
    if (target.contentView.isChildContent()) {
        target.playheadStepTickOffset.set(telemetry.playheadStepTickOffset);
    } else if (needsIntraStepOffset) {
        target.playheadStepTickOffset.set(
            projectedExpandedTelemetryOffset(
                telemetry.expandedVariationTelemetry,
                telemetry.playheadStepTickOffset
            )
        );
    } else if (target.playheadStepTickOffset.get() != 0) {
        target.playheadStepTickOffset.set(0);
    }

    if (target.probabilityCycleIndex != telemetry.probabilityCycleIndex ||
        target.probabilityCycleMask != telemetry.probabilityCycleMask) {
        target.probabilityCycleIndex = telemetry.probabilityCycleIndex;
        target.probabilityCycleMask = telemetry.probabilityCycleMask;
        target.probabilityCycleRevision.set(target.probabilityCycleRevision.get() + 1U);
    }

    if (target.variationTelemetryRevision.get() != telemetry.variationTelemetryRevision) {
        target.lastResolvedVariation = telemetry.lastResolvedVariation;
        target.cycleVariationTelemetry = telemetry.cycleVariationTelemetry;
        target.variationTelemetryRevision.set(telemetry.variationTelemetryRevision);
    }
}

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const oc::note::sequencer::StepSequencerRuntimeState& runtimeState) {
    publishRuntimeTelemetry(target, captureRuntimeTelemetry(runtimeState));
}

}  // namespace core::sequencer

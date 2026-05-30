#include "sequencer/SequencerRuntimeStateSync.hpp"

#include <algorithm>

namespace core::sequencer {

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerState& source,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings
) {
    return captureRuntimeStateSignature(source.pattern, projectScaleSettings);
}

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerPatternState& source,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings
) {
    return {
        .length = source.length.get(),
        .stepsPerBeat = source.stepsPerBeat.get(),
        .midiChannel = source.midiChannel.get(),
        .enabledMask = source.enabledMask.get(),
        .stepDataRevision = source.stepDataRevision.get(),
        .patternVariationRevision = source.patternVariationRevision.get(),
        .patternScaleRevision = source.patternScaleRevision.get(),
        .graphRevision = source.graphRevision.get(),
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
        .stepsPerBeat = source.stepsPerBeat,
        .midiChannel = source.midiChannel,
        .enabledMask = source.enabledMask,
        .stepDataRevision = source.stepDataRevision,
        .patternVariationRevision = source.patternVariationRevision,
        .patternScaleRevision = source.patternScaleRevision,
        .graphRevision = source.graphRevision,
        .effectiveScaleSettings = source.effectiveScaleSettings,
    };
}

void syncRuntimeState(oc::note::sequencer::StepSequencerRuntimeState& target,
                      const core::state::sequencer::SequencerPatternSnapshot& source) {
    target.length = source.length;
    target.stepsPerBeat = source.stepsPerBeat;
    target.midiChannel = source.midiChannel;
    target.enabledMask = source.enabledMask;
    target.scaleSettings = source.effectiveScaleSettings;
    target.scaleSettings.clamp();
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
        .probabilityCycleIndex = runtimeState.probabilityCycleIndex,
        .probabilityCycleMask = runtimeState.probabilityCycleMask,
        .variationTelemetryRevision = runtimeState.variationTelemetryRevision,
        .lastResolvedVariation = runtimeState.lastResolvedVariation,
        .cycleVariationTelemetry = runtimeState.cycleVariationTelemetry,
    };
}

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const SequencerRuntimeTelemetrySnapshot& telemetry) {
    target.playheadStep.set(telemetry.playheadStep);

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

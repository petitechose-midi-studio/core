#include "sequencer/SequencerRuntimeStateSync.hpp"

#include <algorithm>

namespace core::sequencer {

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerState& source
) {
    return {
        .length = source.length.get(),
        .stepsPerBeat = source.stepsPerBeat.get(),
        .midiChannel = source.midiChannel.get(),
        .enabledMask = source.enabledMask.get(),
        .stepDataRevision = source.stepDataRevision.get(),
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
    };
}

void syncRuntimeState(oc::note::sequencer::StepSequencerRuntimeState& target,
                      const core::state::sequencer::SequencerState& source) {
    target.length = source.length.get();
    target.stepsPerBeat = source.stepsPerBeat.get();
    target.midiChannel = source.midiChannel.get();
    target.enabledMask = source.enabledMask.get();

    std::copy(source.note.begin(), source.note.end(), target.note.begin());
    std::copy(source.velocity.begin(), source.velocity.end(), target.velocity.begin());
    std::copy(source.gate.begin(), source.gate.end(), target.gate.begin());
    std::copy(source.nudge.begin(), source.nudge.end(), target.nudge.begin());
    std::copy(source.probability.begin(), source.probability.end(), target.probability.begin());
}

void syncRuntimeState(oc::note::sequencer::StepSequencerRuntimeState& target,
                      const core::state::sequencer::SequencerPatternSnapshot& source) {
    target.length = source.length;
    target.stepsPerBeat = source.stepsPerBeat;
    target.midiChannel = source.midiChannel;
    target.enabledMask = source.enabledMask;

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
}

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const oc::note::sequencer::StepSequencerRuntimeState& runtimeState) {
    publishRuntimeTelemetry(target, captureRuntimeTelemetry(runtimeState));
}

}  // namespace core::sequencer

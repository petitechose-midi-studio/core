#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::sequencer {

struct SequencerRuntimeStateSignature {
    uint8_t length = 0;
    uint8_t stepsPerBeat = 0;
    uint8_t midiChannel = 0;
    oc::note::sequencer::StepBitMask128 enabledMask{};
    uint32_t stepDataRevision = 0;

    bool matches(const SequencerRuntimeStateSignature& other) const {
        return length == other.length &&
               stepsPerBeat == other.stepsPerBeat &&
               midiChannel == other.midiChannel &&
               enabledMask == other.enabledMask &&
               stepDataRevision == other.stepDataRevision;
    }
};

struct SequencerRuntimeTelemetrySnapshot {
    int16_t playheadStep = -1;
    uint32_t probabilityCycleIndex = 0;
    oc::note::sequencer::StepBitMask128 probabilityCycleMask{};
};

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerState& source
);

SequencerRuntimeStateSignature captureRuntimeStateSignature(
    const core::state::sequencer::SequencerPatternSnapshot& source
);

void syncRuntimeState(oc::note::sequencer::StepSequencerRuntimeState& target,
                      const core::state::sequencer::SequencerState& source);

void syncRuntimeState(oc::note::sequencer::StepSequencerRuntimeState& target,
                      const core::state::sequencer::SequencerPatternSnapshot& source);

SequencerRuntimeTelemetrySnapshot captureRuntimeTelemetry(
    const oc::note::sequencer::StepSequencerRuntimeState& runtimeState
);

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const SequencerRuntimeTelemetrySnapshot& telemetry);

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const oc::note::sequencer::StepSequencerRuntimeState& runtimeState);

}  // namespace core::sequencer

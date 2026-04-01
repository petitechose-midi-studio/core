#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerRuntimeState.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::sequencer {

struct SequencerRuntimeStateSignature {
    uint8_t length = 0;
    uint8_t stepsPerBeat = 0;
    uint8_t midiChannel = 0;
    uint64_t enabledMask = 0;
    uint32_t stepDataRevision = 0;

    bool matches(const SequencerRuntimeStateSignature& other) const {
        return length == other.length &&
               stepsPerBeat == other.stepsPerBeat &&
               midiChannel == other.midiChannel &&
               enabledMask == other.enabledMask &&
               stepDataRevision == other.stepDataRevision;
    }
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

void publishRuntimeTelemetry(core::state::sequencer::SequencerState& target,
                             const oc::note::sequencer::StepSequencerRuntimeState& runtimeState);

}  // namespace core::sequencer

#pragma once

#include <array>
#include <cstdint>

#include <oc/note/sequencer/StepBitMask128.hpp>

#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

struct SequencerPatternSnapshot {
    uint8_t length = oc::note::sequencer::StepSequencerState::DEFAULT_LENGTH;
    uint8_t stepsPerBeat = oc::note::sequencer::StepSequencerState::DEFAULT_STEPS_PER_BEAT;
    uint8_t midiChannel = oc::note::sequencer::StepSequencerState::DEFAULT_MIDI_CHANNEL_0BASED;
    oc::note::sequencer::StepBitMask128 enabledMask{};
    uint32_t stepDataRevision = 0;
    std::array<uint8_t, SequencerState::MAX_STEPS> note{};
    std::array<uint8_t, SequencerState::MAX_STEPS> velocity{};
    std::array<uint16_t, SequencerState::MAX_STEPS> gate{};
    std::array<int8_t, SequencerState::MAX_STEPS> nudge{};
    std::array<uint8_t, SequencerState::MAX_STEPS> probability{};
};

struct SequencerTrackBankSnapshot {
    uint8_t activeTrack = 0;
    uint16_t enabledMask = 0x0001;
    std::array<SequencerPatternSnapshot, SequencerTrackBankState::TRACK_COUNT> tracks{};
};

}  // namespace core::state::sequencer

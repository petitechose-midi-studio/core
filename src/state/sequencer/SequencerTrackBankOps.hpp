#pragma once

#include <cstdint>

#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::state::sequencer {

struct SequencerTrackBankSnapshot {
    uint8_t activeTrack = 0;
    uint8_t enabledMask = 0x01;
    std::array<SequencerPatternSnapshot, SequencerTrackBankState::TRACK_COUNT> tracks{};
};

void initializeTrackBankFromActive(SequencerTrackBankState& bank, const SequencerState& active);

void storeActiveTrack(SequencerTrackBankState& bank, const SequencerState& active);

bool switchActiveTrack(SequencerTrackBankState& bank, SequencerState& active, uint8_t nextTrack);

void captureTrackBankSnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    SequencerTrackBankSnapshot& out
);

void applyTrackBankSnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerTrackBankSnapshot& snapshot
);

}  // namespace core::state::sequencer

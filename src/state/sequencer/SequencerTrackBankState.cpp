#include "state/sequencer/SequencerTrackBankState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

FLASHMEM void SequencerTrackSelectorState::reset(uint8_t track) {
    selecting.set(false);
    selectedTrack.set(track);
    snapshotTrack = track;
    snapshotEnabledMask = 0x01;
}

FLASHMEM SequencerTrackBankState::SequencerTrackBankState()
    : activeTrack{0}, enabledMask{0x01}, selector{}, tracks{} {}

FLASHMEM void SequencerTrackBankState::reset() {
    activeTrack.set(0);
    enabledMask.set(0x01);
    selector.reset(0);
    selector.snapshotEnabledMask = 0x01;

    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        auto& seq = tracks[i];
        seq.reset();
        seq.midiChannel.set(i);
    }
}

}  // namespace core::state::sequencer

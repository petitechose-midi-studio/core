#include "state/sequencer/SequencerTrackBankState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

FLASHMEM SequencerTrackBankState::SequencerTrackBankState()
    : activeTrack{0}, enabledMask{0x0001}, tracks{} {}

FLASHMEM void SequencerTrackBankState::reset() {
    activeTrack.set(0);
    enabledMask.set(0x0001);

    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        auto& seq = tracks[i];
        seq.reset();
        seq.midiChannel.set(i);
    }
}

}  // namespace core::state::sequencer

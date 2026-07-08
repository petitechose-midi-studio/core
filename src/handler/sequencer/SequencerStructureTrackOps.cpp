#include "handler/sequencer/SequencerStructureTrackOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

FLASHMEM bool createSequencerStructureTrack(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::TrackNavigationState& trackUi,
    const SharedTrackDomainServices& sharedTracks
) {
    namespace structure_slots = core::state::shared;

    const uint16_t enabledMask = sharedTracks.enabledMask();
    const uint8_t activeTrack = sharedTracks.activeTrack();
    const uint8_t index = trackUi.previewAddSlot.get()
        ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
              trackUi.previewTrackIndex.get()
          )
        : activeTrack;
    if ((enabledMask & structure_slots::slotBit(index)) != 0) {
        return false;
    }

    core::state::sequencer::storeActiveTrack(tracks, sequencer);
    tracks.track(index).reset();
    tracks.track(index).midiChannel.set(index);
    return sharedTracks.setState(
        static_cast<uint16_t>(enabledMask | structure_slots::slotBit(index)),
        index
    );
}

}  // namespace core::handler

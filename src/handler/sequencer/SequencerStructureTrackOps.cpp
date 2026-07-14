#include "handler/sequencer/SequencerStructureTrackOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

FLASHMEM uint8_t sequencerStructureTrackTarget(
    const core::state::TrackNavigationState& trackUi,
    uint8_t activeTrack
) {
    return trackUi.previewAddSlot.get()
        ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
              trackUi.previewTrackIndex.get()
          )
        : core::state::sequencer::SequencerTrackBankState::clampTrackIndex(activeTrack);
}

FLASHMEM bool createSequencerStructureTrack(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::TrackNavigationState& trackUi,
    const SharedTrackDomainServices& sharedTracks
) {
    namespace structure_slots = core::state::shared;

    const uint16_t enabledMask = sharedTracks.enabledMask();
    const uint8_t index = sequencerStructureTrackTarget(trackUi, sharedTracks.activeTrack());
    if ((enabledMask & structure_slots::slotBit(index)) != 0) {
        return false;
    }

    if (!core::state::sequencer::storeActiveTrack(tracks, sequencer)) return false;
    auto& destination = tracks.track(index);
    const uint8_t destinationMidiChannel = destination.midiChannel.get();
    destination.reset();
    destination.midiChannel.set(destinationMidiChannel);
    return sharedTracks.setState(
        static_cast<uint16_t>(enabledMask | structure_slots::slotBit(index)),
        index
    );
}

FLASHMEM bool toggleSequencerStructureTrackMute(
    core::state::sequencer::SequencerTrackBankState& tracks,
    uint8_t track
) {
    const uint8_t index =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(track);
    const bool nextMuted = !tracks.isTrackMuted(index);
    return tracks.setTrackMuted(index, nextMuted);
}

}  // namespace core::handler

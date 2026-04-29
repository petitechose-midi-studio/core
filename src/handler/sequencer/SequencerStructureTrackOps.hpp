#pragma once

#include "handler/common/SharedTrackDomainServices.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/shared/StructureSlotOps.hpp"

namespace core::handler {

/**
 * Shared sequencer-only track creation primitive.
 *
 * This is used by structure navigation and paste-to-add-slot editing. Macro
 * track creation stays in MacroStructureDomainServices because it owns
 * persistence, page presentation, and runtime sync.
 */
inline bool createSequencerStructureTrack(
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

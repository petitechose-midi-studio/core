#include "handler/sequencer/SequencerStructureTrackOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"
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

    core::state::sequencer::storeActiveTrack(tracks, sequencer);
    tracks.track(index).reset();
    tracks.track(index).midiChannel.set(index);
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

FLASHMEM bool pasteCurrentSequencerStructureTrack(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    const SharedTrackDomainServices& sharedTracks,
    const core::state::StructureClipboardState& structureClipboard
) {
    if (!structureClipboard.hasSequencerTrack()) return false;
    if (trackUi.previewAddSlot.get() &&
        !createSequencerStructureTrack(sequencer, tracks, trackUi, sharedTracks)) {
        return false;
    }

    core::state::sequencer::applySnapshotToEditor(
        sequencer,
        structureClipboard.sequencerTrack
    );
    core::state::sequencer::copyGraph(
        sequencer.pattern,
        structureClipboard.sequencerGraph.get(),
        structureClipboard.sequencerTrack.graphRevision
    );
    core::state::sequencer::storeActiveTrack(tracks, sequencer);
    return true;
}

}  // namespace core::handler

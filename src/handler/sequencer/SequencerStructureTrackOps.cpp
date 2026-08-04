#include "handler/sequencer/SequencerStructureTrackOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

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

FLASHMEM bool toggleSequencerStructureTrackMute(
    const core::state::project::ProjectTrackState& tracks,
    core::state::project::ProjectTrackDomainServices& trackDomain,
    uint8_t track
) {
    if (!core::state::project::validProjectTrackIndex(track)) return false;
    return trackDomain.setMuted(
        track,
        !core::state::project::projectTrackMuted(tracks, track)
    );
}

}  // namespace core::handler

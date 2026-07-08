#pragma once

#include "handler/common/SharedTrackDomainServices.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

/**
 * Shared sequencer-only track creation primitive.
 *
 * This is used by structure navigation and paste-to-add-slot editing. Macro
 * track creation stays in MacroStructureDomainServices because it owns
 * persistence, page presentation, and runtime sync.
 */
bool createSequencerStructureTrack(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::TrackNavigationState& trackUi,
    const SharedTrackDomainServices& sharedTracks
);

}  // namespace core::handler

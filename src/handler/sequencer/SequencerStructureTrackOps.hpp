#pragma once

#include "handler/common/SharedTrackDomainServices.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {

uint8_t sequencerStructureTrackTarget(
    const core::state::TrackNavigationState& trackUi,
    uint8_t activeTrack
);

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

bool toggleSequencerStructureTrackMute(
    core::state::sequencer::SequencerTrackBankState& tracks,
    uint8_t track
);

bool pasteCurrentSequencerStructureTrack(
    core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    const SharedTrackDomainServices& sharedTracks,
    const core::state::StructureClipboardState& structureClipboard
);

}  // namespace core::handler

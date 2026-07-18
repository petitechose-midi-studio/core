#pragma once

#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

SequencerInteractionContext makeSequencerInteractionContext(
    const SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible = false
);

}  // namespace core::state::sequencer

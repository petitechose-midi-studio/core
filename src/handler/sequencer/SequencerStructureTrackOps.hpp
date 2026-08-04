#pragma once

#include "state/TrackNavigationState.hpp"
#include "state/project/ProjectTrackDomainServices.hpp"
#include "state/project/ProjectTrackState.hpp"

namespace core::handler {

uint8_t sequencerStructureTrackTarget(
    const core::state::TrackNavigationState& trackUi,
    uint8_t activeTrack
);

bool toggleSequencerStructureTrackMute(
    const core::state::project::ProjectTrackState& tracks,
    core::state::project::ProjectTrackDomainServices& trackDomain,
    uint8_t track
);

}  // namespace core::handler

#pragma once

#include <cstdint>

#include "state/sequencer/SequencerCcLaneRouting.hpp"
#include "state/shared/MidiCcDestinationResolver.hpp"

namespace core::handler {

class MidiCcGlobalFrameCoordinator;

struct SequencerCcLaneLiveProjection {
    bool lanePresent = false;
    bool hasOutput = false;
    bool conflict = false;
    uint8_t outputValue = 0;
    core::state::shared::MidiCcCandidateClass winnerClass =
        core::state::shared::MidiCcCandidateClass::SEQUENCER_CC_LANE;
};

/** Read the last committed arbiter frame for one exact Track/Lane author. */
SequencerCcLaneLiveProjection projectSequencerCcLaneLive(
    const MidiCcGlobalFrameCoordinator* coordinator,
    core::state::sequencer::SequencerCcLaneAddress address,
    const core::state::sequencer::SequencerCcLane& lane,
    const core::state::sequencer::SequencerCcTrackRoute& trackRoute
);

}  // namespace core::handler

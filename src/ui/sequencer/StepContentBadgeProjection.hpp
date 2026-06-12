#pragma once

#include <cstdint>

#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerPatternState.hpp"

namespace core::ui::sequencer::grid {

struct StepContentBadgeProjection {
    bool microSequence = false;
    bool cycleStates = false;
};

StepContentBadgeProjection buildStepContentBadgeProjection(
    const core::state::sequencer::SequencerPatternState& pattern,
    uint8_t absoluteStep
);
StepContentBadgeProjection buildStepContentBadgeProjectionForNode(
    const core::state::sequencer::SequencerPatternState& pattern,
    core::state::sequencer::SequencerGraphNodeId nodeId
);

}  // namespace core::ui::sequencer::grid

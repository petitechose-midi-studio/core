#pragma once

#include <cstdint>

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

}  // namespace core::ui::sequencer::grid

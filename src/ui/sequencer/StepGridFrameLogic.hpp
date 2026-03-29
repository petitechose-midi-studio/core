#pragma once

#include "state/sequencer/SequencerState.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"

namespace core::ui::sequencer::grid {

StepGridFrameState buildStepGridFrameState(const core::state::sequencer::SequencerState& sequencerState);

}  // namespace core::ui::sequencer::grid

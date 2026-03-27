#pragma once

#include "state/CoreState.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"

namespace core::ui::sequencer::grid {

StepGridFrameState buildStepGridFrameState(const core::state::CoreState& coreState);

}  // namespace core::ui::sequencer::grid

#pragma once

#include "state/sequencer/SequencerState.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"

namespace core::ui::sequencer::grid {

/**
 * Builds a visible-page step-grid frame from SequencerState.
 *
 * This function reads musical data, active property, inline feedback, and
 * playhead status; it does not plan diffs or touch LVGL widgets.
 */
StepGridFrameState buildStepGridFrameState(const core::state::sequencer::SequencerState& sequencerState);

}  // namespace core::ui::sequencer::grid

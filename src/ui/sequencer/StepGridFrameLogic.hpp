#pragma once

#include "state/sequencer/SequencerState.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"

namespace core::ui::sequencer::grid {

/**
 * Builds a visible-page step-grid frame from the resolved sequencer display
 * projection.
 *
 * This function adapts domain-level resolved musical facts to render state; it
 * does not resolve music, plan diffs, or touch LVGL widgets.
 */
StepGridFrameState buildStepGridFrameState(
    const core::state::sequencer::SequencerState& sequencerState,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings = {},
    bool stepFocusActive = false
);

}  // namespace core::ui::sequencer::grid

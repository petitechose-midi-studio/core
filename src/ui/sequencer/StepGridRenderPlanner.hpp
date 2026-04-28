#pragma once

#include <array>

#include "ui/sequencer/StepGridLabelLogic.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"

namespace core::ui::sequencer::grid {

/**
 * Plans the minimal step-grid render work for the next frame.
 *
 * The planner compares frame state with cached LVGL render data and marks dirty
 * tiles. It does not calculate geometry or draw widgets.
 */
struct FrameRenderPlan {
    bool propertyVisualChanged = false;
    InlineFeedbackSnapshot nextFeedback{};
    std::array<TileRenderDiff, 8> diffs{};
    std::array<bool, 8> feedbackChanged{};
    std::array<bool, 8> tileDirty{};
    bool anyDirty = false;
};

FrameRenderPlan buildFrameRenderPlan(const std::array<TileRenderCache, 8>& caches,
                                     core::state::sequencer::StepProperty cachedProperty,
                                     const InlineFeedbackSnapshot& cachedFeedback,
                                     const StepGridFrameState& frameState);

}  // namespace core::ui::sequencer::grid

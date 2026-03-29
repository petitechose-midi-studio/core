#pragma once

#include <array>

#include "ui/sequencer/StepGridLabelLogic.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"

namespace core::ui::sequencer::grid {

struct FrameRenderPlan {
    bool propertyVisualChanged = false;
    bool selectionChanged = false;
    InlineFeedbackSnapshot nextFeedback{};
    std::array<TileRenderDiff, 8> diffs{};
    std::array<bool, 8> feedbackChanged{};
    std::array<bool, 8> tileDirty{};
    bool anyDirty = false;
};

FrameRenderPlan buildFrameRenderPlan(const std::array<TileRenderCache, 8>& caches,
                                     core::state::sequencer::StepProperty cachedProperty,
                                     const InlineFeedbackSnapshot& cachedFeedback,
                                     const RangeSelectionSnapshot& cachedSelection,
                                     const StepGridFrameState& frameState);

}  // namespace core::ui::sequencer::grid

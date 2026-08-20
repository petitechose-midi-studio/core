#include "ui/sequencer/StepGridRenderPlanner.hpp"

#include "ui/sequencer/StepGridLabelLogic.hpp"
#include "ui/sequencer/StepGridRenderLogic.hpp"

namespace core::ui::sequencer::grid {

namespace {

bool feedbackActiveForStep(const InlineFeedbackSnapshot& feedback, uint8_t absoluteStep) {
    return feedback.visible && feedback.touchedMask.test(absoluteStep);
}

bool tileFeedbackChanged(uint8_t absoluteStep,
                         const InlineFeedbackSnapshot& before,
                         const InlineFeedbackSnapshot& after) {
    const bool beforeActive = feedbackActiveForStep(before, absoluteStep);
    const bool afterActive = feedbackActiveForStep(after, absoluteStep);
    if (beforeActive != afterActive) return true;
    if (!(beforeActive || afterActive)) return false;
    return before.property != after.property;
}

}  // namespace

FrameRenderPlan buildFrameRenderPlan(const std::array<TileRenderCache, 8>& caches,
                                     core::state::sequencer::StepProperty cachedProperty,
                                     const InlineFeedbackSnapshot& cachedFeedback,
                                     const StepGridFrameState& frameState) {
    FrameRenderPlan plan;
    uint8_t noteLayerDirtyMask = 0U;
    plan.propertyVisualChanged = frameState.activeProperty != cachedProperty;
    plan.nextFeedback = readInlineFeedbackSnapshot(
        frameState.feedbackVisible,
        frameState.feedbackTouchedMask,
        frameState.feedbackProperty
    );

    for (uint8_t i = 0; i < frameState.tiles.size(); ++i) {
        const TileRenderState& state = frameState.tiles[i];
        plan.diffs[i] = diffTileRenderState(caches[i], state);
        plan.feedbackChanged[i] =
            tileFeedbackChanged(state.absoluteStep, cachedFeedback, plan.nextFeedback);
        if (plan.diffs[i].noteEventsChanged) {
            noteLayerDirtyMask = static_cast<uint8_t>(
                noteLayerDirtyMask |
                caches[i].noteEvents.coveredTileMask(i) |
                state.noteEvents.coveredTileMask(i)
            );
        }
    }

    for (uint8_t i = 0; i < frameState.tiles.size(); ++i) {
        const bool nonPlayheadDirty =
            plan.diffs[i].dataChanged ||
            plan.diffs[i].probabilityMaskChanged ||
            plan.diffs[i].contentBadgesChanged ||
            plan.propertyVisualChanged ||
            plan.feedbackChanged[i] ||
            (noteLayerDirtyMask & static_cast<uint8_t>(1U << i)) != 0U;
        plan.tileDirty[i] = nonPlayheadDirty || plan.diffs[i].playheadChanged;
        plan.playheadOnly[i] = plan.diffs[i].playheadChanged && !nonPlayheadDirty;
        plan.anyDirty = plan.anyDirty || plan.tileDirty[i];
    }

    return plan;
}

}  // namespace core::ui::sequencer::grid

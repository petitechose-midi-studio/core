#include "ui/sequencer/StepGridLabelLogic.hpp"

#include "ui/sequencer/StepGridRenderLogic.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

namespace core::ui::sequencer::grid {

namespace {

core::state::sequencer::StepProperty displayPropertyForInlineLabelMode(
    visual::InlineLabelMode mode
) {
    using core::state::sequencer::StepProperty;

    switch (mode) {
        case visual::InlineLabelMode::VELOCITY:
            return StepProperty::VELOCITY;
        case visual::InlineLabelMode::GATE:
            return StepProperty::GATE;
        case visual::InlineLabelMode::NUDGE:
            return StepProperty::NUDGE;
        case visual::InlineLabelMode::PROBABILITY:
            return StepProperty::PROBABILITY;
        case visual::InlineLabelMode::NOTE:
        case visual::InlineLabelMode::NONE:
        default:
            return StepProperty::NOTE;
    }
}

}  // namespace

InlineFeedbackSnapshot readInlineFeedbackSnapshot(
    bool visible,
    oc::note::sequencer::StepBitMask128 touchedMask,
    core::state::sequencer::StepProperty property
) {
    return {
        .visible = visible,
        .touchedMask = touchedMask,
        .property = property,
    };
}

NoteLabelPresentation buildNoteLabelPresentation(
    const TileRenderState& state,
    const StepGridFrameState& frameState
) {
    NoteLabelPresentation presentation;
    const auto propertyVisual = visual::buildStepPropertyVisual(
        frameState.activeProperty,
        state.inPattern
    );

    if (!state.inPattern || !propertyVisual.showNoteLabel ||
        propertyVisual.inlineLabelMode == visual::InlineLabelMode::NONE) {
        return presentation;
    }

    const bool isNoteMode = propertyVisual.inlineLabelMode == visual::InlineLabelMode::NOTE;
    const bool isFeedbackStep =
        frameState.feedbackVisible &&
        frameState.feedbackTouchedMask.test(state.absoluteStep);
    const bool isFeedbackProperty =
        frameState.feedbackProperty == frameState.activeProperty;
    const bool showRuntimePitch = hasRuntimePitchFeedback(state);
    const bool showRuntimeActiveProperty = hasRuntimePropertyFeedback(
        state,
        frameState.activeProperty
    );

    presentation.probabilityMasked = state.enabled && !state.probabilityCycleActive;
    presentation.showLabel =
        showRuntimeActiveProperty ||
        showRuntimePitch ||
        (isNoteMode && state.stepSelectionCursor) ||
        (state.childContentContext && state.stepSelectionCursor) ||
        (isFeedbackStep && isFeedbackProperty);
    presentation.showInlineIcon =
        !state.childContentContext &&
        !showRuntimePitch &&
        propertyVisual.showInlineIcon &&
        isFeedbackStep &&
        isFeedbackProperty;
    presentation.displayProperty =
        state.childContentContext
            ? frameState.activeProperty
            : showRuntimeActiveProperty
            ? frameState.activeProperty
            : showRuntimePitch
            ? core::state::sequencer::StepProperty::NOTE
            : displayPropertyForInlineLabelMode(propertyVisual.inlineLabelMode);
    if (frameState.presentation == StepGridPresentation::MELODIC &&
        presentation.displayProperty ==
            core::state::sequencer::StepProperty::NOTE) {
        // Pitch is already encoded by rail height. The exact note/degree lives
        // in the focused header, leaving the musical surface unobstructed.
        presentation.showLabel = false;
        presentation.showInlineIcon = false;
        return presentation;
    }
    if (frameState.presentation == StepGridPresentation::DRUM_LANE) {
        // Drum pitch is owned by the lane, so repeating it in every child tile
        // adds noise and suggests a value that cannot be edited here. Scalar
        // text is limited to the focused/touched tile; the retained geometry
        // already communicates Velocity, Gate and Nudge across the grid.
        presentation.showLabel =
            presentation.displayProperty !=
                core::state::sequencer::StepProperty::NOTE &&
            (state.stepSelectionCursor ||
             (isFeedbackStep && isFeedbackProperty));
        presentation.showInlineIcon =
            presentation.showLabel && propertyVisual.showInlineIcon;
        return presentation;
    }
    return presentation;
}

}  // namespace core::ui::sequencer::grid

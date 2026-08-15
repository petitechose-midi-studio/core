#include "ui/sequencer/StepGridLabelLogic.hpp"

#include "ui/sequencer/StepGridRenderLogic.hpp"

namespace core::ui::sequencer::grid {

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
    const visual::StepPropertyVisualSpec& propertyVisual,
    core::state::sequencer::StepProperty activeProperty,
    const InlineFeedbackSnapshot& feedback,
    StepGridPresentation presentationMode
) {
    NoteLabelPresentation presentation;

    if (!state.inPattern || !propertyVisual.showNoteLabel ||
        propertyVisual.inlineLabelMode == visual::InlineLabelMode::NONE) {
        return presentation;
    }

    const bool isNoteMode = propertyVisual.inlineLabelMode == visual::InlineLabelMode::NOTE;
    const bool isFeedbackStep =
        feedback.visible &&
        feedback.touchedMask.test(state.absoluteStep);
    const bool isFeedbackProperty = feedback.property == activeProperty;
    const bool showRuntimePitch = hasRuntimePitchFeedback(state);
    const bool showRuntimeActiveProperty = hasRuntimePropertyFeedback(state, activeProperty);

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
            ? activeProperty
            : showRuntimeActiveProperty
            ? activeProperty
            : showRuntimePitch
            ? core::state::sequencer::StepProperty::NOTE
            : displayPropertyForInlineLabelMode(propertyVisual.inlineLabelMode);
    if (presentationMode == StepGridPresentation::MELODIC &&
        presentation.displayProperty ==
            core::state::sequencer::StepProperty::NOTE) {
        // Pitch is already encoded by rail height. The exact note/degree lives
        // in the focused header, leaving the musical surface unobstructed.
        presentation.showLabel = false;
        presentation.showInlineIcon = false;
        presentation.showNoteStyle = false;
        return presentation;
    }
    if (presentationMode == StepGridPresentation::DRUM_LANE) {
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
        presentation.showNoteStyle = false;
        return presentation;
    }
    presentation.showNoteStyle =
        presentation.displayProperty == core::state::sequencer::StepProperty::NOTE &&
        (isNoteMode || showRuntimePitch);
    return presentation;
}

}  // namespace core::ui::sequencer::grid

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
    const InlineFeedbackSnapshot& feedback
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

    presentation.showNoteStyle = isNoteMode || showRuntimePitch;
    presentation.probabilityMasked = state.enabled && !state.probabilityCycleActive;
    presentation.showLabel = showRuntimePitch || isNoteMode || (isFeedbackStep && isFeedbackProperty);
    presentation.showInlineIcon =
        !showRuntimePitch && propertyVisual.showInlineIcon && isFeedbackStep && isFeedbackProperty;
    presentation.displayProperty =
        showRuntimePitch
            ? core::state::sequencer::StepProperty::NOTE
            : displayPropertyForInlineLabelMode(propertyVisual.inlineLabelMode);
    return presentation;
}

}  // namespace core::ui::sequencer::grid

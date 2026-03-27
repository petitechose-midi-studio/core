#include "ui/sequencer/StepGridLabelLogic.hpp"

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
    uint8_t stepIndex,
    core::state::sequencer::StepProperty property
) {
    return {
        .visible = visible,
        .stepIndex = stepIndex,
        .property = property,
    };
}

bool inlineFeedbackChanged(const InlineFeedbackSnapshot& lhs, const InlineFeedbackSnapshot& rhs) {
    return lhs.visible != rhs.visible ||
           lhs.stepIndex != rhs.stepIndex ||
           lhs.property != rhs.property;
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
    const bool isFeedbackStep = feedback.visible && state.absoluteStep == feedback.stepIndex;
    const bool isFeedbackProperty = feedback.property == activeProperty;

    presentation.showNoteStyle = isNoteMode;
    presentation.probabilityMasked = state.enabled && !state.probabilityCycleActive;
    presentation.showLabel = isNoteMode || (isFeedbackStep && isFeedbackProperty);
    presentation.showInlineIcon =
        propertyVisual.showInlineIcon && isFeedbackStep && isFeedbackProperty;
    presentation.displayProperty =
        displayPropertyForInlineLabelMode(propertyVisual.inlineLabelMode);
    return presentation;
}

}  // namespace core::ui::sequencer::grid

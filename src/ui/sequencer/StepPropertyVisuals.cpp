#include "StepPropertyVisuals.hpp"

#include "ui/font/StandaloneIcons.hpp"

namespace core::ui::sequencer::visual {

const char* propertyIconGlyph(core::state::sequencer::StepProperty property) {
    using core::state::sequencer::StepProperty;

    switch (property) {
        case StepProperty::NOTE:
            return standalone::icons::NOTE_PROP_PITCH;
        case StepProperty::VELOCITY:
            return standalone::icons::NOTE_PROP_VEL;
        case StepProperty::GATE:
            return standalone::icons::NOTE_PROP_GATE;
        case StepProperty::NUDGE:
            return standalone::icons::NOTE_PROP_NUDGE;
        case StepProperty::PROBABILITY:
            return standalone::icons::NOTE_PROP_RANDOM;
        default:
            return standalone::icons::NOTE_PROP_PITCH;
    }
}

StepPropertyVisualSpec buildStepPropertyVisual(
    core::state::sequencer::StepProperty property,
    bool inPattern
) {
    StepPropertyVisualSpec spec;

    if (!inPattern) {
        return spec;
    }

    switch (property) {
        case core::state::sequencer::StepProperty::NOTE:
            spec.inlineLabelMode = InlineLabelMode::NOTE;
            spec.showNoteLabel = true;
            break;
        case core::state::sequencer::StepProperty::VELOCITY:
            spec.inlineLabelMode = InlineLabelMode::VELOCITY;
            spec.showNoteLabel = true;
            break;
        case core::state::sequencer::StepProperty::GATE:
            spec.inlineLabelMode = InlineLabelMode::GATE;
            spec.showNoteLabel = true;
            break;
        case core::state::sequencer::StepProperty::NUDGE:
            spec.inlineLabelMode = InlineLabelMode::NUDGE;
            spec.showNoteLabel = true;
            break;
        case core::state::sequencer::StepProperty::PROBABILITY:
            spec.inlineLabelMode = InlineLabelMode::PROBABILITY;
            spec.showNoteLabel = true;
            spec.showInlineIcon = true;
            break;
        default:
            break;
    }

    return spec;
}

}  // namespace core::ui::sequencer::visual

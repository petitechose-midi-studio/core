#include "StepPropertyVisuals.hpp"

#include <algorithm>

#include "ui/font/StandaloneIcons.hpp"

namespace core::ui::sequencer::visual {

namespace {

constexpr uint8_t VELOCITY_MAX = 127;
constexpr uint8_t PROBABILITY_MAX = 100;
constexpr int8_t NUDGE_MAX = 50;
constexpr lv_opa_t WATERMARK_NOTE_OPA = LV_OPA_20;
constexpr lv_opa_t WATERMARK_VALUE_OPA = LV_OPA_30;

uint8_t clampPercent(uint32_t value, uint32_t maxValue) {
    if (maxValue == 0) return 0;
    const uint32_t clamped = std::min(value, maxValue);
    return static_cast<uint8_t>((clamped * 100U + (maxValue / 2U)) / maxValue);
}

}  // namespace

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
    const StepPropertyVisualInput& input
) {
    StepPropertyVisualSpec spec;
    spec.icon = propertyIconGlyph(property);
    spec.showWatermark = input.inPattern;
    spec.showNoteLabel = input.inPattern && property == core::state::sequencer::StepProperty::NOTE;
    spec.watermarkOpa =
        (property == core::state::sequencer::StepProperty::NOTE) ? WATERMARK_NOTE_OPA
                                                                 : WATERMARK_VALUE_OPA;

    if (!input.inPattern) {
        return spec;
    }

    switch (property) {
        case core::state::sequencer::StepProperty::NOTE:
            spec.valueBarMode = PropertyValueBarMode::NONE;
            break;
        case core::state::sequencer::StepProperty::VELOCITY:
            spec.valueBarMode = PropertyValueBarMode::UNIPOLAR;
            spec.valueBarPercent = clampPercent(input.velocity, VELOCITY_MAX);
            break;
        case core::state::sequencer::StepProperty::GATE:
            spec.showHorizontalAccent = true;
            spec.edgeTickMode = PropertyEdgeTickMode::END;
            break;
        case core::state::sequencer::StepProperty::PROBABILITY:
            spec.valueBarMode = PropertyValueBarMode::UNIPOLAR;
            spec.valueBarPercent = clampPercent(input.probability, PROBABILITY_MAX);
            break;
        case core::state::sequencer::StepProperty::NUDGE: {
            spec.showHorizontalAccent = true;
            spec.edgeTickMode = PropertyEdgeTickMode::START;
            break;
        }
        default:
            break;
    }

    return spec;
}

}  // namespace core::ui::sequencer::visual

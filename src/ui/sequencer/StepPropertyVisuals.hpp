#pragma once

/**
 * @file StepPropertyVisuals.hpp
 * @brief Shared visual rules for per-step property overlays in the sequencer grid.
 */

#include <cstdint>

#include <lvgl.h>

#include "state/sequencer/SequencerState.hpp"

namespace core::ui::sequencer::visual {

enum class PropertyValueBarMode : uint8_t {
    NONE = 0,
    UNIPOLAR = 1,
    BIPOLAR = 2,
};

enum class PropertyEdgeTickMode : uint8_t {
    NONE = 0,
    START = 1,
    END = 2,
};

struct StepPropertyVisualInput {
    bool inPattern = false;
    bool enabled = false;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t probability = 0;
    uint16_t gate = 0;
    int8_t nudge = 0;
};

struct StepPropertyVisualSpec {
    const char* icon = "";
    bool showWatermark = false;
    bool showNoteLabel = false;
    bool showHorizontalAccent = false;
    PropertyValueBarMode valueBarMode = PropertyValueBarMode::NONE;
    PropertyEdgeTickMode edgeTickMode = PropertyEdgeTickMode::NONE;
    uint8_t valueBarPercent = 0;
    bool valueBarPositive = true;
    lv_opa_t watermarkOpa = LV_OPA_TRANSP;
};

const char* propertyIconGlyph(core::state::sequencer::StepProperty property);

StepPropertyVisualSpec buildStepPropertyVisual(
    core::state::sequencer::StepProperty property,
    const StepPropertyVisualInput& input
);

}  // namespace core::ui::sequencer::visual

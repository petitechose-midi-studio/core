#pragma once

/**
 * @file StepPropertyVisuals.hpp
 * @brief Shared visual rules for per-step property overlays in the sequencer grid.
 */

#include <cstdint>

#include "state/sequencer/SequencerUiState.hpp"

namespace core::ui::sequencer::visual {

enum class InlineLabelMode : uint8_t {
    NONE = 0,
    NOTE = 1,
    VELOCITY = 2,
    GATE = 3,
    NUDGE = 4,
    PROBABILITY = 5,
};

struct StepPropertyVisualSpec {
    InlineLabelMode inlineLabelMode = InlineLabelMode::NONE;
    bool showInlineIcon = false;
    bool showNoteLabel = false;
};

struct DrumPropertyVisualSpec {
    const char* icon = "";
    const char* label = "";
    uint32_t color = 0U;
};

const char* propertyIconGlyph(core::state::sequencer::StepProperty property);

StepPropertyVisualSpec buildStepPropertyVisual(
    core::state::sequencer::StepProperty property,
    bool inPattern
);

DrumPropertyVisualSpec buildDrumPropertyVisual(
    core::state::sequencer::DrumSequencerProperty property
);

}  // namespace core::ui::sequencer::visual

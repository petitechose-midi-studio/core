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
    InlineLabelMode inlineLabelMode = InlineLabelMode::NONE;
    bool showInlineIcon = false;
    bool showNoteLabel = false;
};

const char* propertyIconGlyph(core::state::sequencer::StepProperty property);

StepPropertyVisualSpec buildStepPropertyVisual(
    core::state::sequencer::StepProperty property,
    const StepPropertyVisualInput& input
);

}  // namespace core::ui::sequencer::visual

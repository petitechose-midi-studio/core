#pragma once

#include <array>
#include <cstdint>

#include <lvgl.h>

#include "state/sequencer/SequencerUiState.hpp"

namespace core::ui::sequencer::grid {

struct TileRenderState {
    uint8_t absoluteStep = 0;
    bool inPattern = false;
    bool enabled = false;
    bool playing = false;
    bool probabilityCycleActive = false;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t probability = 0;
    uint16_t gate = 0;
    int8_t nudge = 0;
};

struct TileRenderDiff {
    bool initialized = false;
    bool inPatternChanged = false;
    bool enabledChanged = false;
    bool noteChanged = false;
    bool velocityChanged = false;
    bool probabilityChanged = false;
    bool probabilityCycleActiveChanged = false;
    bool gateChanged = false;
    bool nudgeChanged = false;
    bool velocityZeroChanged = false;
    bool dataChanged = false;
    bool barChanged = false;
};

struct TileRenderCache {
    bool initialized = false;
    bool inPattern = false;
    bool enabled = false;
    bool playing = false;
    bool probabilityCycleActive = false;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t probability = 0;
    uint16_t gate = 0;
    int8_t nudge = 0;
    lv_coord_t noteLabelHeight = 0;
};

struct StepGridFrameState {
    core::state::sequencer::StepProperty activeProperty =
        core::state::sequencer::StepProperty::NOTE;
    bool feedbackVisible = false;
    uint8_t feedbackStep = 0xFF;
    core::state::sequencer::StepProperty feedbackProperty =
        core::state::sequencer::StepProperty::NOTE;
    std::array<TileRenderState, 8> tiles{};
};

struct StepVisualStyle {
    lv_coord_t width = 6;
    lv_coord_t height = 18;
    lv_coord_t x = 0;
    lv_coord_t y = 56 - 2 - 1 - 18;
    lv_color_t strokeColor = lv_color_hex(0);
    lv_opa_t strokeOpa = LV_OPA_COVER;
};

}  // namespace core::ui::sequencer::grid

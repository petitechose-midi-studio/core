#pragma once

#include <array>
#include <cstdint>
#include <cstring>

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
    bool absoluteStepChanged = false;
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
    lv_coord_t noteLabelHeight = 0;
    bool noteLabelVisible = false;
    bool inlineIconVisible = false;
    uint32_t noteLabelColorFull = 0;
    lv_opa_t noteLabelOpa = LV_OPA_TRANSP;
    uint32_t inlineIconColorFull = 0;
    lv_opa_t inlineIconOpa = LV_OPA_TRANSP;
    lv_coord_t noteLabelX = 0;
    lv_coord_t noteLabelY = 0;
    lv_coord_t inlineIconX = 0;
    lv_coord_t inlineIconY = 0;
    char noteLabelText[16] = {0};
    bool shapeVisible = false;
    lv_coord_t shapeX = 0;
    lv_coord_t shapeY = 0;
    lv_coord_t shapeWidth = 0;
    lv_coord_t shapeHeight = 0;
    uint32_t shapeStrokeColor = 0;
    lv_opa_t shapeStrokeOpa = LV_OPA_TRANSP;
    bool markerVisible = false;
    lv_coord_t markerX = 0;
    lv_coord_t markerY = 0;
    uint32_t markerColor = 0;
    lv_opa_t markerOpa = LV_OPA_TRANSP;
    bool indicatorVisible = false;
    lv_opa_t indicatorOpa = LV_OPA_TRANSP;
    bool selectionDotVisible = false;
    uint32_t selectionDotColor = 0;
    lv_opa_t selectionDotOpa = LV_OPA_TRANSP;
    char stepIndexText[4] = {0};
};

struct RangeSelectionSnapshot {
    bool active = false;
    core::state::sequencer::RangeSelectionKind kind =
        core::state::sequencer::RangeSelectionKind::NONE;
    core::state::sequencer::RangeSelectionPhase phase =
        core::state::sequencer::RangeSelectionPhase::IDLE;
    uint8_t cursorStep = 0;
    bool sourceRangeVisible = false;
    uint8_t sourceStart = 0;
    uint8_t sourceEnd = 0;
};

struct StepGridFrameState {
    core::state::sequencer::StepProperty activeProperty =
        core::state::sequencer::StepProperty::NOTE;
    bool feedbackVisible = false;
    uint64_t feedbackTouchedMask = 0;
    core::state::sequencer::StepProperty feedbackProperty =
        core::state::sequencer::StepProperty::NOTE;
    RangeSelectionSnapshot selection{};
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

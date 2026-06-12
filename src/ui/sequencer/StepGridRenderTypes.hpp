#pragma once

#include <array>
#include <cstdint>
#include <cstring>

#include <lvgl.h>
#include <oc/note/sequencer/StepBitMask128.hpp>
#include <oc/note/sequencer/StepSequencerVariation.hpp>

#include "state/sequencer/SequencerUiState.hpp"
#include "ui/sequencer/StepContentBadgeProjection.hpp"

namespace core::ui::sequencer::grid {

struct TileVariationRenderState {
    bool visible = false;
    bool rangeVisible = false;
    bool deltaVisible = false;
    core::state::sequencer::StepProperty rangeProperty =
        core::state::sequencer::StepProperty::NOTE;
    oc::note::sequencer::StepSequencerResolvedVariation resolved{};
};

using TileContentBadgeState = StepContentBadgeProjection;

/**
 * Data exchanged between step-grid projection, planning, and rendering.
 *
 * Frame state is the current desired visual model, cache stores what LVGL last
 * rendered, and diffs tell renderers which tile surfaces must be updated.
 */
struct TileRenderState {
    uint8_t absoluteStep = 0;
    bool inPattern = false;
    bool enabled = false;
    bool playheadVisible = false;
    bool playing = false;
    bool probabilityCycleActive = false;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t probability = 0;
    uint16_t gate = 0;
    int8_t nudge = 0;
    bool childContentContext = false;
    int16_t childContentOffset = 0;
    bool childContentNoteOffsetUsesScaleDegrees = false;
    bool childPitchSummaryVisible = false;
    uint8_t childPitchSummaryNote = 0;
    TileVariationRenderState variation{};
    TileContentBadgeState contentBadges{};
};

struct TileRenderDiff {
    bool initialized = false;
    bool absoluteStepChanged = false;
    bool inPatternChanged = false;
    bool enabledChanged = false;
    bool playheadVisibleChanged = false;
    bool noteChanged = false;
    bool velocityChanged = false;
    bool probabilityChanged = false;
    bool probabilityCycleActiveChanged = false;
    bool gateChanged = false;
    bool nudgeChanged = false;
    bool childContentChanged = false;
    bool childPitchSummaryChanged = false;
    bool variationChanged = false;
    bool contentBadgesChanged = false;
    bool velocityZeroChanged = false;
    bool probabilityMaskChanged = false;
    bool dataChanged = false;
    bool barChanged = false;
};

struct TileRenderCache {
    bool initialized = false;
    uint8_t absoluteStep = 0;
    bool inPattern = false;
    bool enabled = false;
    bool playheadVisible = false;
    bool playing = false;
    bool probabilityCycleActive = false;
    uint8_t note = 0;
    uint8_t velocity = 0;
    uint8_t probability = 0;
    uint16_t gate = 0;
    int8_t nudge = 0;
    bool childContentContext = false;
    int16_t childContentOffset = 0;
    bool childContentNoteOffsetUsesScaleDegrees = false;
    bool childPitchSummaryVisible = false;
    uint8_t childPitchSummaryNote = 0;
    TileVariationRenderState variation{};
    TileContentBadgeState contentBadges{};
    lv_coord_t noteLabelHeight = 0;
    bool noteLabelVisible = false;
    bool originalNoteLabelVisible = false;
    bool inlineIconVisible = false;
    uint32_t noteLabelColorFull = 0;
    lv_opa_t noteLabelOpa = LV_OPA_TRANSP;
    uint32_t originalNoteLabelColorFull = 0;
    lv_opa_t originalNoteLabelOpa = LV_OPA_TRANSP;
    uint32_t inlineIconColorFull = 0;
    lv_opa_t inlineIconOpa = LV_OPA_TRANSP;
    lv_coord_t noteLabelX = 0;
    lv_coord_t noteLabelY = 0;
    lv_coord_t originalNoteLabelX = 0;
    lv_coord_t originalNoteLabelY = 0;
    lv_coord_t inlineIconX = 0;
    lv_coord_t inlineIconY = 0;
    char noteLabelText[16] = {0};
    char originalNoteLabelText[12] = {0};
    bool shapeVisible = false;
    lv_coord_t shapeX = 0;
    lv_coord_t shapeY = 0;
    lv_coord_t shapeWidth = 0;
    lv_coord_t shapeHeight = 0;
    uint32_t shapeStrokeColor = 0;
    lv_opa_t shapeStrokeOpa = LV_OPA_TRANSP;
    bool indicatorVisible = false;
    uint32_t indicatorColorFull = 0;
    lv_opa_t indicatorOpa = LV_OPA_TRANSP;
    char stepIndexText[4] = {0};
};

struct StepGridFrameState {
    core::state::sequencer::StepProperty activeProperty =
        core::state::sequencer::StepProperty::NOTE;
    bool feedbackVisible = false;
    oc::note::sequencer::StepBitMask128 feedbackTouchedMask{};
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

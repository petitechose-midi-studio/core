#pragma once

#include <cstdint>

#include <lvgl.h>

#include "ui/sequencer/StepGridRenderTypes.hpp"

namespace core::ui::sequencer::grid {

/**
 * Pure visual calculations for step tile styling and tile diffs.
 *
 * This header owns note/velocity/probability colors, shape sizing, and
 * per-field diff rules. LVGL object mutation belongs to renderers/widgets.
 */
inline constexpr uint8_t VELOCITY_MAX = 127;
inline constexpr int8_t NUDGE_VISUAL_MAX = 50;
inline constexpr lv_coord_t STEP_BUTTON_SIZE = 56;
inline constexpr lv_coord_t STEP_SHAPE_PAD_X = 0;
inline constexpr lv_coord_t STEP_SHAPE_PAD_Y = 1;
inline constexpr lv_coord_t STEP_SHAPE_MIN_WIDTH = 6;
inline constexpr lv_coord_t STEP_SHAPE_MIN_HEIGHT = 18;
inline constexpr lv_coord_t STEP_BAR_HEIGHT = 2;
inline constexpr lv_opa_t STEP_SHAPE_OPA_ENABLED = LV_OPA_COVER;
inline constexpr lv_opa_t STEP_SHAPE_OPA_DISABLED = LV_OPA_COVER;
inline constexpr lv_opa_t STEP_SHAPE_OPA_VELOCITY_ZERO = LV_OPA_COVER;
inline constexpr uint8_t SHAPE_DISABLED_FILL_BRIGHTNESS = 20;
inline constexpr uint8_t VELOCITY_ZERO_FILL_BRIGHTNESS = 22;
inline constexpr lv_opa_t STEP_NOTE_LABEL_LIGHTEN = static_cast<lv_opa_t>(48);
inline constexpr uint8_t PROBABILITY_ICON_MIN_BRIGHTNESS = 28;

lv_color_t noteLabelColor(uint8_t note);
lv_color_t probabilityInlineIconColor(uint8_t note, uint8_t probability);
int scaleDegreeIndexForNote(oc::note::sequencer::StepSequencerScaleSettings settings,
                            uint8_t note);
const char* scaleDegreeLabel(int degree);
bool hasRuntimePitchFeedback(const TileRenderState& state);
bool hasRuntimeVelocityFeedback(const TileRenderState& state);
bool hasRuntimeGateFeedback(const TileRenderState& state);
bool hasRuntimeNudgeFeedback(const TileRenderState& state);
bool hasRuntimePropertyFeedback(
    const TileRenderState& state,
    core::state::sequencer::StepProperty property
);
bool hasOutOfScaleFeedback(const TileRenderState& state);
bool hasScaleDegreeFeedback(const TileRenderState& state);
bool hasConstrainedScaleDegreeFeedback(const TileRenderState& state);
uint8_t runtimePitchDisplayNote(const TileRenderState& state);
uint8_t runtimeVelocityDisplayValue(const TileRenderState& state);
uint16_t runtimeGateDisplayValue(const TileRenderState& state);
int8_t runtimeNudgeDisplayValue(const TileRenderState& state);
const char* runtimeScaleDegreeLabel(const TileRenderState& state);
StepVisualStyle buildStepVisualStyle(uint8_t note,
                                     uint8_t velocity,
                                     uint16_t gate,
                                     int8_t nudge,
                                     bool enabled,
                                     lv_coord_t railWidth,
                                     lv_coord_t buttonHeight);
TileRenderDiff diffTileRenderState(const TileRenderCache& cache, const TileRenderState& state);

}  // namespace core::ui::sequencer::grid

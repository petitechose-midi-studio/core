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
inline constexpr lv_coord_t STEP_BOTTOM_RESERVED_HEIGHT = 2;
inline constexpr uint8_t SHAPE_DISABLED_FILL_BRIGHTNESS = 20;
inline constexpr uint8_t VELOCITY_ZERO_FILL_BRIGHTNESS = 22;
inline constexpr lv_opa_t STEP_NOTE_LABEL_LIGHTEN = static_cast<lv_opa_t>(48);
inline constexpr uint8_t PROBABILITY_ICON_MIN_BRIGHTNESS = 28;

lv_color_t noteLabelColor(uint8_t note);
lv_color_t probabilityInlineIconColor(uint8_t note, uint8_t probability);
lv_color_t drumLaneAccentColor(uint32_t accentColor, uint8_t velocity, bool enabled);
lv_color_t stepEventAccentColor(uint32_t accentColor, uint8_t velocity);
enum class StepPitchOverflow : uint8_t {
    NONE = 0,
    BELOW,
    ABOVE,
};
StepPitchViewport buildStepPitchViewport(
    const std::array<TileRenderState, 8>& tiles
);
StepPitchOverflow stepPitchOverflow(
    uint8_t note,
    const StepPitchViewport& viewport
);
lv_coord_t stepPitchY(
    uint8_t note,
    const StepPitchViewport& viewport,
    lv_coord_t buttonHeight
);
uint8_t stepEventHeadHeight(uint8_t velocity);
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
TileRenderDiff diffTileRenderState(const TileRenderCache& cache, const TileRenderState& state);

}  // namespace core::ui::sequencer::grid

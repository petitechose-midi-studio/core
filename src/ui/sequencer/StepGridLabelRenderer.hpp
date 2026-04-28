#pragma once

#include <lvgl.h>

#include "ui/sequencer/StepGridLabelLogic.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

namespace core::ui::sequencer::grid::label_renderer {

/**
 * Applies label/icon render decisions to LVGL objects for one tile.
 *
 * The renderer uses frame state, diffs, geometry, and cached values to update
 * only the affected note label and inline icon widgets.
 */
void renderTileNoteLabel(uint8_t tileIndex,
                         TileRenderCache& cache,
                         lv_obj_t* noteLabel,
                         lv_obj_t* inlineIcon,
                         const TileRenderState& state,
                         const TileRenderDiff& diff,
                         bool propertyVisualChanged,
                         bool tileFeedbackChanged,
                         core::state::sequencer::StepProperty activeProperty,
                         const InlineFeedbackSnapshot& feedback,
                         const visual::StepPropertyVisualSpec& propertyVisual,
                         lv_coord_t noteBaseX,
                         lv_coord_t noteLabelY,
                         lv_coord_t noteLabelHeight,
                         lv_coord_t inlineIconWidth,
                         lv_coord_t inlineIconHeight);

}  // namespace core::ui::sequencer::grid::label_renderer

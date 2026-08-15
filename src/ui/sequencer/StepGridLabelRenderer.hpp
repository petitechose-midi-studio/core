#pragma once

#include <lvgl.h>

#include "ui/sequencer/StepGridLabelLogic.hpp"
#include "ui/sequencer/StepGridRenderTypes.hpp"

namespace core::ui::sequencer::grid::label_renderer {

struct TileLabelWidgets {
    lv_obj_t* primary = nullptr;
    lv_obj_t* secondary = nullptr;
    lv_obj_t* inlineIcon = nullptr;
};

struct TileLabelGeometry {
    lv_coord_t baseX = 0;
    lv_coord_t baselineY = 0;
    lv_coord_t labelHeight = 0;
    lv_coord_t iconWidth = 0;
    lv_coord_t iconHeight = 0;
};

/**
 * Applies label/icon render decisions to LVGL objects for one tile.
 *
 * The renderer uses frame state, diffs, geometry, and cached values to update
 * only the affected note label and inline icon widgets.
 */
void renderTileNoteLabel(TileRenderCache& cache,
                         const TileLabelWidgets& widgets,
                         const TileRenderState& state,
                         const TileRenderDiff& diff,
                         bool propertyVisualChanged,
                         bool tileFeedbackChanged,
                         bool geometryChanged,
                         const StepGridFrameState& frameState,
                         const TileLabelGeometry& geometry);

}  // namespace core::ui::sequencer::grid::label_renderer

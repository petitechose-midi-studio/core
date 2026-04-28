#pragma once

#include <cstdint>

#include <lvgl.h>

namespace core::ui::sequencer::grid {

/**
 * Pure geometry calculations for step-grid labels, markers, and guides.
 *
 * Functions consume LVGL areas and measured sizes but return layout values only;
 * callers apply positions to widgets.
 */
struct StepGuideLayout {
    lv_coord_t x = 0;
    lv_coord_t y = 0;
    lv_coord_t height = 0;
};

struct StepTileGeometry {
    lv_coord_t railWidth = 0;
    lv_coord_t buttonHeight = 0;
    lv_coord_t noteBaseX = 0;
    lv_coord_t noteBaseY = 0;
    lv_coord_t noteLabelBaselineY = 0;
};

struct InlineLabelLayout {
    lv_coord_t iconX = 0;
    lv_coord_t iconY = 0;
    lv_coord_t labelX = 0;
    lv_coord_t labelY = 0;
};

lv_coord_t measureRailWidth(lv_coord_t contentWidth);
lv_coord_t measureButtonHeight(lv_coord_t contentHeight);
StepGuideLayout buildGuideLayout(uint8_t guideIndex, lv_coord_t railWidth, lv_coord_t buttonHeight);
StepTileGeometry buildTileGeometry(const lv_area_t& buttonArea,
                                   const lv_area_t& noteLayerArea,
                                   lv_coord_t railWidth,
                                   lv_coord_t buttonHeight);
lv_point_t buildMarkerPosition(lv_coord_t noteBaseX, lv_coord_t noteBaseY);
InlineLabelLayout buildInlineLabelLayout(lv_coord_t noteBaseX,
                                         lv_coord_t noteLabelBaselineY,
                                         lv_coord_t noteLabelHeight,
                                         bool showInlineIcon,
                                         lv_coord_t inlineIconWidth,
                                         lv_coord_t inlineIconHeight);

}  // namespace core::ui::sequencer::grid

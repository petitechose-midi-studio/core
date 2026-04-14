#pragma once

#include <cstdint>

#include <lvgl.h>

namespace core::ui::sequencer::grid::widgets {

void createRoot(lv_obj_t* parent,
                lv_obj_t*& container,
                lv_obj_t*& grid,
                lv_obj_t*& noteLayer,
                lv_event_cb_t geometryEvent,
                void* geometryUserData);

void createTile(uint8_t tileIndex,
                lv_obj_t* grid,
                lv_obj_t* noteLayer,
                lv_obj_t*& tile,
                lv_obj_t*& noteLabel,
                lv_obj_t*& stepInlineIcon,
                lv_obj_t*& stepButton,
                lv_obj_t*& stepShape,
                lv_obj_t*& stepMarker,
                lv_coord_t& inlineIconWidth,
                lv_coord_t& inlineIconHeight,
                lv_event_cb_t geometryEvent,
                void* geometryUserData);
lv_coord_t noteLabelHeight();

}  // namespace core::ui::sequencer::grid::widgets

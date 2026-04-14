#pragma once

#include <cstdint>

#include <lvgl.h>

namespace core::ui::add_slot_icon {

inline constexpr lv_coord_t LONG_AXIS = 6;
inline constexpr lv_coord_t SHORT_AXIS = 2;

struct ObjectPair {
    lv_obj_t* horizontal = nullptr;
    lv_obj_t* vertical = nullptr;
};

ObjectPair createCentered(lv_obj_t* parent, uint32_t colorHex);
void setVisible(const ObjectPair& icon, bool visible);
void drawCentered(lv_layer_t* layer, const lv_area_t& area, uint32_t colorHex, lv_opa_t opa);

}  // namespace core::ui::add_slot_icon

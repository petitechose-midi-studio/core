#include "ui/common/AddSlotIcon.hpp"

#include <config/PlatformCompat.hpp>

namespace core::ui::add_slot_icon {

namespace {

FLASHMEM lv_obj_t* createBar(
    lv_obj_t* parent,
    lv_coord_t width,
    lv_coord_t height,
    uint32_t colorHex
) {
    lv_obj_t* bar = lv_obj_create(parent);
    lv_obj_remove_style_all(bar);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, width, height);
    lv_obj_set_style_bg_color(bar, lv_color_hex(colorHex), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_center(bar);
    return bar;
}

}  // namespace

FLASHMEM ObjectPair createCentered(lv_obj_t* parent, uint32_t colorHex) {
    return {
        .horizontal = createBar(parent, LONG_AXIS, SHORT_AXIS, colorHex),
        .vertical = createBar(parent, SHORT_AXIS, LONG_AXIS, colorHex),
    };
}

void setVisible(const ObjectPair& icon, bool visible) {
    if (!icon.horizontal || !icon.vertical) return;

    if (visible) {
        lv_obj_clear_flag(icon.horizontal, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(icon.vertical, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_add_flag(icon.horizontal, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(icon.vertical, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace core::ui::add_slot_icon

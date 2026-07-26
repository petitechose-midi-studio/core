#include "ui/common/AddSlotIcon.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::ui::add_slot_icon {

namespace {

lv_coord_t evenFloor(lv_coord_t value) {
    return static_cast<lv_coord_t>(value - (value & 1));
}

lv_area_t centeredArea(const lv_area_t& area, lv_coord_t width, lv_coord_t height) {
    const lv_coord_t areaWidth = lv_area_get_width(&area);
    const lv_coord_t areaHeight = lv_area_get_height(&area);
    const lv_coord_t x =
        static_cast<lv_coord_t>(area.x1 + std::max<lv_coord_t>(0, (areaWidth - width) / 2));
    const lv_coord_t y =
        static_cast<lv_coord_t>(area.y1 + std::max<lv_coord_t>(0, (areaHeight - height) / 2));
    return lv_area_t{
        .x1 = x,
        .y1 = y,
        .x2 = static_cast<lv_coord_t>(x + width - 1),
        .y2 = static_cast<lv_coord_t>(y + height - 1),
    };
}

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

void drawCentered(lv_layer_t* layer, const lv_area_t& area, uint32_t colorHex, lv_opa_t opa) {
    if (!layer || opa == LV_OPA_TRANSP) return;

    const lv_coord_t horizontalWidth = std::max<lv_coord_t>(
        SHORT_AXIS,
        std::min<lv_coord_t>(LONG_AXIS, evenFloor(lv_area_get_width(&area)))
    );
    const lv_coord_t verticalHeight = std::max<lv_coord_t>(
        SHORT_AXIS,
        std::min<lv_coord_t>(LONG_AXIS, evenFloor(lv_area_get_height(&area)))
    );
    const lv_area_t horizontalArea = centeredArea(area, horizontalWidth, SHORT_AXIS);
    const lv_area_t verticalArea = centeredArea(area, SHORT_AXIS, verticalHeight);

    lv_draw_rect_dsc_t rectDsc;
    lv_draw_rect_dsc_init(&rectDsc);
    rectDsc.bg_color = lv_color_hex(colorHex);
    rectDsc.bg_opa = opa;
    rectDsc.radius = 0;
    rectDsc.border_width = 0;

    lv_draw_rect(layer, &rectDsc, &horizontalArea);
    lv_draw_rect(layer, &rectDsc, &verticalArea);
}

}  // namespace core::ui::add_slot_icon

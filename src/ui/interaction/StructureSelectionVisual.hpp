#pragma once

#include <algorithm>
#include <cstdint>

#include <lvgl.h>

#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::interaction {

enum class StructureSelectionVisualRole : uint8_t {
    SELECTED = 0,
    CURSOR,
    DESTINATION_FREE,
    DESTINATION_OVERWRITE,
    DESTINATION_BLOCKED,
};

constexpr uint32_t structureSelectionColor(
    StructureSelectionVisualRole role
) {
    namespace color = standalone::theme::color;
    switch (role) {
        case StructureSelectionVisualRole::CURSOR:
            return color::FOCUS_EDIT;
        case StructureSelectionVisualRole::DESTINATION_FREE:
            return color::POSITIVE;
        case StructureSelectionVisualRole::DESTINATION_OVERWRITE:
            return color::WARNING;
        case StructureSelectionVisualRole::DESTINATION_BLOCKED:
            return color::DESTRUCTIVE;
        case StructureSelectionVisualRole::SELECTED:
        default:
            return color::ROUTING;
    }
}

inline constexpr lv_opa_t STRUCTURE_SELECTION_FILL_OPA = LV_OPA_20;
inline constexpr lv_opa_t STRUCTURE_DESTINATION_FILL_OPA = LV_OPA_30;
inline constexpr lv_opa_t STRUCTURE_SELECTION_OUTLINE_OPA = LV_OPA_80;
inline constexpr lv_coord_t STRUCTURE_SELECTION_CORNER_LENGTH = 7;
inline constexpr lv_coord_t STRUCTURE_SELECTION_CORNER_THICKNESS = 2;

inline void drawStructureSelectionCorners(
    lv_layer_t* layer,
    const lv_area_t& area,
    uint32_t colorHex,
    lv_opa_t opacity = LV_OPA_COVER
) {
    if (!layer || opacity == LV_OPA_TRANSP) return;

    const lv_coord_t width = lv_area_get_width(&area);
    const lv_coord_t height = lv_area_get_height(&area);
    if (width <= 0 || height <= 0) return;

    const lv_coord_t corner = std::min<lv_coord_t>(
        STRUCTURE_SELECTION_CORNER_LENGTH,
        std::max<lv_coord_t>(1, std::min(width, height) / 2)
    );
    const lv_coord_t thickness = std::min<lv_coord_t>(
        STRUCTURE_SELECTION_CORNER_THICKNESS,
        std::min(width, height)
    );

    lv_draw_rect_dsc_t dsc;
    lv_draw_rect_dsc_init(&dsc);
    dsc.bg_color = lv_color_hex(colorHex);
    dsc.bg_opa = opacity;
    dsc.radius = 0;
    dsc.border_width = 0;

    const auto draw = [&](lv_coord_t x, lv_coord_t y, lv_coord_t w, lv_coord_t h) {
        const lv_area_t segment{
            .x1 = x,
            .y1 = y,
            .x2 = static_cast<lv_coord_t>(x + w - 1),
            .y2 = static_cast<lv_coord_t>(y + h - 1),
        };
        lv_draw_rect(layer, &dsc, &segment);
    };

    draw(area.x1, area.y1, corner, thickness);
    draw(area.x1, area.y1, thickness, corner);
    draw(static_cast<lv_coord_t>(area.x2 - corner + 1), area.y1, corner, thickness);
    draw(static_cast<lv_coord_t>(area.x2 - thickness + 1), area.y1, thickness, corner);
    draw(area.x1, static_cast<lv_coord_t>(area.y2 - thickness + 1), corner, thickness);
    draw(area.x1, static_cast<lv_coord_t>(area.y2 - corner + 1), thickness, corner);
    draw(
        static_cast<lv_coord_t>(area.x2 - corner + 1),
        static_cast<lv_coord_t>(area.y2 - thickness + 1),
        corner,
        thickness
    );
    draw(
        static_cast<lv_coord_t>(area.x2 - thickness + 1),
        static_cast<lv_coord_t>(area.y2 - corner + 1),
        thickness,
        corner
    );
}

static_assert(sizeof(StructureSelectionVisualRole) == 1U);

}  // namespace core::ui::interaction

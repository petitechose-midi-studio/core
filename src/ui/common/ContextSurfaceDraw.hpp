#pragma once

#include <array>
#include <lvgl.h>
#include <src/misc/lv_area_private.h>
#include <ms/ui/widget/TextOverflow.hpp>

namespace core::ui::surface {

/** Retain caller-owned text without splitting UTF-8 or allocating. */
template <size_t N>
bool copyText(std::array<char, N>& target, const char* source) {
    std::array<char, N> next{};
    ms::ui::text::formatEllipsized(next.data(), next.size(), source, nullptr, 0);
    if (target == next) return false;
    target = next;
    return true;
}

/** Shared drawing primitives; geometry and appearance never decide an action. */
inline lv_area_t area(const lv_area_t& origin, int x, int y, int width, int height) {
    return {origin.x1 + x, origin.y1 + y,
            origin.x1 + x + width - 1, origin.y1 + y + height - 1};
}

inline void fill(lv_layer_t* layer, const lv_area_t& bounds, uint32_t color,
                 lv_opa_t opacity = LV_OPA_COVER, int radius = 0) {
    if (bounds.x2 < bounds.x1 || bounds.y2 < bounds.y1) return;
    lv_draw_rect_dsc_t descriptor;
    lv_draw_rect_dsc_init(&descriptor);
    descriptor.bg_color = lv_color_hex(color);
    descriptor.bg_opa = opacity;
    descriptor.radius = radius;
    lv_draw_rect(layer, &descriptor, &bounds);
}

inline void text(lv_layer_t* layer, const lv_area_t& bounds, const char* value,
                 const lv_font_t* font, uint32_t color,
                 lv_opa_t opacity = LV_OPA_COVER,
                 lv_text_align_t align = LV_TEXT_ALIGN_LEFT) {
    if (!value || !value[0] || bounds.x2 < bounds.x1 || bounds.y2 < bounds.y1) return;
    lv_draw_label_dsc_t descriptor;
    lv_draw_label_dsc_init(&descriptor);
    descriptor.text = value;
    descriptor.font = font ? font : LV_FONT_DEFAULT;
    descriptor.color = lv_color_hex(color);
    descriptor.opa = opacity;
    descriptor.align = align;
    descriptor.flag = LV_TEXT_FLAG_EXPAND;
    // A long name must not paint over a neighbouring command or the transport.
    const auto previous_clip = layer->_clip_area;
    if (lv_area_intersect(&layer->_clip_area, &previous_clip, &bounds)) {
        lv_draw_label(layer, &descriptor, &bounds);
    }
    layer->_clip_area = previous_clip;
}

}  // namespace core::ui::surface

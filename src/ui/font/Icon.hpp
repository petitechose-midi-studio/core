// Auto-generated | 1 icons | 2026-01-02
#pragma once
#include "StandaloneFonts.hpp"

#include <lvgl.h>

namespace Icon {
enum class Size : uint8_t { S = 12, M = 14, L = 16 };

    constexpr const char* TRANSPORT_PLAY = "\xEE\x80\x80";

inline void set(lv_obj_t* label, const char* icon, Size size = Size::M) {
    lv_font_t* font = (size == Size::S) ? standalone_fonts.icons_12
                        : (size == Size::M) ? standalone_fonts.icons_14
                        : standalone_fonts.icons_16;
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, icon);
}
}  // namespace Icon
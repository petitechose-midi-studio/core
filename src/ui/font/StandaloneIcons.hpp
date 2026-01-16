// Auto-generated | 5 icons | 2026-01-16
#pragma once
#include "StandaloneFonts.hpp"

#include <lvgl.h>

namespace standalone::icons {
enum class Size : uint8_t { S = 12, M = 14, L = 16 };

    constexpr const char* KNOB = "\xEE\x80\x80";
    constexpr const char* MIDI_CC = "\xEE\x80\x81";
    constexpr const char* MIDI_CHANNEL = "\xEE\x80\x82";
    constexpr const char* NOTE = "\xEE\x80\x83";
    constexpr const char* TRANSPORT_PLAY = "\xEE\x80\x84";

inline void set(lv_obj_t* label, const char* icon, Size size = Size::M) {
    lv_font_t* font = (size == Size::S) ? standalone_fonts.icons_12
                        : (size == Size::M) ? standalone_fonts.icons_14
                        : standalone_fonts.icons_16;
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, icon);
}
}  // namespace standalone::icons
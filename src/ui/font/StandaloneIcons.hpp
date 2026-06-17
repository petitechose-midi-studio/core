// Auto-generated | 35 icons | 2026-06-16
#pragma once
#include "StandaloneFonts.hpp"

#include <lvgl.h>

namespace standalone::icons {
enum class Size : uint8_t { S = 12, M = 14, L = 16 };

    constexpr const char* ACTION_BACKWARD = "\xEE\x80\x80";
    constexpr const char* ACTION_CANCEL = "\xEE\x80\x81";
    constexpr const char* ACTION_CLEAR = "\xEE\x80\x82";
    constexpr const char* ACTION_COPY = "\xEE\x80\x83";
    constexpr const char* ACTION_PASTE = "\xEE\x80\x84";
    constexpr const char* ACTION_PLACE_TARGET = "\xEE\x80\x85";
    constexpr const char* ACTION_REDO = "\xEE\x80\x86";
    constexpr const char* ACTION_UNDO = "\xEE\x80\x87";
    constexpr const char* ACTION_VALIDATE = "\xEE\x80\x88";
    constexpr const char* CLOCK_MASTER = "\xEE\x80\x89";
    constexpr const char* CLOCK_SLAVE = "\xEE\x80\x8A";
    constexpr const char* CYCLE_STATE = "\xEE\x80\x8B";
    constexpr const char* DIVISION = "\xEE\x80\x8C";
    constexpr const char* HOME = "\xEE\x80\x8D";
    constexpr const char* KNOB = "\xEE\x80\x8E";
    constexpr const char* LENGTH = "\xEE\x80\x8F";
    constexpr const char* LOCK = "\xEE\x80\x90";
    constexpr const char* MICRO_SEQUENCE = "\xEE\x80\x91";
    constexpr const char* MIDI_CC = "\xEE\x80\x92";
    constexpr const char* MIDI_CHANNEL = "\xEE\x80\x93";
    constexpr const char* MODIFIER_SHIFT = "\xEE\x80\x94";
    constexpr const char* NOTE = "\xEE\x80\x95";
    constexpr const char* NOTE_PROP_GATE = "\xEE\x80\x96";
    constexpr const char* NOTE_PROP_NUDGE = "\xEE\x80\x97";
    constexpr const char* NOTE_PROP_PITCH = "\xEE\x80\x98";
    constexpr const char* NOTE_PROP_RANDOM = "\xEE\x80\x99";
    constexpr const char* NOTE_PROP_VEL = "\xEE\x80\x9A";
    constexpr const char* OFFSET = "\xEE\x80\x9B";
    constexpr const char* ROUTING = "\xEE\x80\x9C";
    constexpr const char* SCALE = "\xEE\x80\x9D";
    constexpr const char* SETTINGS_GEAR = "\xEE\x80\x9E";
    constexpr const char* STORAGE = "\xEE\x80\x9F";
    constexpr const char* SWING = "\xEE\x80\xA0";
    constexpr const char* TEMPO = "\xEE\x80\xA1";
    constexpr const char* TRANSPORT_PLAY = "\xEE\x80\xA2";

inline void set(lv_obj_t* label, const char* icon, Size size = Size::M) {
    lv_font_t* font = (size == Size::S) ? standalone_fonts.icons_12
                        : (size == Size::M) ? standalone_fonts.icons_14
                        : standalone_fonts.icons_16;
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, icon);
}
}  // namespace standalone::icons
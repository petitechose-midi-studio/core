// Auto-generated | 61 icons | 2026-08-18
#pragma once
#include "StandaloneFonts.hpp"

#include <lvgl.h>

namespace standalone::icons {
enum class Size : uint8_t { S = 12, M = 14, L = 16 };

    constexpr const char* ACTION_APPLY = "\xEE\x80\x80";
    constexpr const char* ACTION_BACKWARD = "\xEE\x80\x81";
    constexpr const char* ACTION_CANCEL = "\xEE\x80\x82";
    constexpr const char* ACTION_CLEAR = "\xEE\x80\x83";
    constexpr const char* ACTION_COPY = "\xEE\x80\x84";
    constexpr const char* ACTION_OVERWRITE = "\xEE\x80\x85";
    constexpr const char* ACTION_PASTE = "\xEE\x80\x86";
    constexpr const char* ACTION_PLACE_TARGET = "\xEE\x80\x87";
    constexpr const char* ACTION_REMOVE = "\xEE\x80\x88";
    constexpr const char* ACTION_RESET = "\xEE\x80\x89";
    constexpr const char* ACTION_VALIDATE = "\xEE\x80\x8A";
    constexpr const char* CHORD = "\xEE\x80\x8B";
    constexpr const char* CHORD_PROP_COLOR = "\xEE\x80\x8C";
    constexpr const char* CHORD_PROP_MODE = "\xEE\x80\x8D";
    constexpr const char* CHORD_PROP_SHAPE = "\xEE\x80\x8E";
    constexpr const char* COLOR_SWATCH = "\xEE\x80\x8F";
    constexpr const char* CYCLE_STATE = "\xEE\x80\x90";
    constexpr const char* DIVISION = "\xEE\x80\x91";
    constexpr const char* DRUM_CLAP = "\xEE\x80\x92";
    constexpr const char* DRUM_CLOSED_HAT = "\xEE\x80\x93";
    constexpr const char* DRUM_GENERIC = "\xEE\x80\x94";
    constexpr const char* DRUM_KICK = "\xEE\x80\x95";
    constexpr const char* DRUM_OPEN_HAT = "\xEE\x80\x96";
    constexpr const char* DRUM_PERCUSSION = "\xEE\x80\x97";
    constexpr const char* DRUM_SNARE = "\xEE\x80\x98";
    constexpr const char* DRUM_TOM = "\xEE\x80\x99";
    constexpr const char* HOME = "\xEE\x80\x9A";
    constexpr const char* KNOB = "\xEE\x80\x9B";
    constexpr const char* LENGTH = "\xEE\x80\x9C";
    constexpr const char* LOCK = "\xEE\x80\x9D";
    constexpr const char* MACRO_AUTOMATION = "\xEE\x80\x9E";
    constexpr const char* MACRO_MODULATION = "\xEE\x80\x9F";
    constexpr const char* MICRO_SEQUENCE = "\xEE\x80\xA0";
    constexpr const char* MIDI_CC = "\xEE\x80\xA1";
    constexpr const char* MIDI_CHANNEL = "\xEE\x80\xA2";
    constexpr const char* MODIFIER_SHIFT = "\xEE\x80\xA3";
    constexpr const char* NOTE = "\xEE\x80\xA4";
    constexpr const char* NOTE_PROP_GATE = "\xEE\x80\xA5";
    constexpr const char* NOTE_PROP_NUDGE = "\xEE\x80\xA6";
    constexpr const char* NOTE_PROP_PITCH = "\xEE\x80\xA7";
    constexpr const char* NOTE_PROP_RANDOM = "\xEE\x80\xA8";
    constexpr const char* NOTE_PROP_VEL = "\xEE\x80\xA9";
    constexpr const char* OFFSET = "\xEE\x80\xAA";
    constexpr const char* ROUTE_PIN = "\xEE\x80\xAB";
    constexpr const char* ROUTING = "\xEE\x80\xAC";
    constexpr const char* SCALE = "\xEE\x80\xAD";
    constexpr const char* SETTINGS_GEAR = "\xEE\x80\xAE";
    constexpr const char* STATUS_CONFLICT = "\xEE\x80\xAF";
    constexpr const char* STATUS_ERROR = "\xEE\x80\xB0";
    constexpr const char* STATUS_PAUSED = "\xEE\x80\xB1";
    constexpr const char* STATUS_PREVIEW = "\xEE\x80\xB2";
    constexpr const char* STATUS_QUEUED = "\xEE\x80\xB3";
    constexpr const char* STATUS_RESUME = "\xEE\x80\xB4";
    constexpr const char* STATUS_WARNING = "\xEE\x80\xB5";
    constexpr const char* STORAGE = "\xEE\x80\xB6";
    constexpr const char* SWING = "\xEE\x80\xB7";
    constexpr const char* TEMPO = "\xEE\x80\xB8";
    constexpr const char* TEXT_NAME = "\xEE\x80\xB9";
    constexpr const char* TRACK_MUTE = "\xEE\x80\xBA";
    constexpr const char* TRACK_SOLO = "\xEE\x80\xBB";
    constexpr const char* TRANSPORT_PLAY = "\xEE\x80\xBC";

inline void set(lv_obj_t* label, const char* icon, Size size = Size::M) {
    lv_font_t* font = (size == Size::S) ? standalone_fonts.icons_12
                        : (size == Size::M) ? standalone_fonts.icons_14
                        : standalone_fonts.icons_16;
    lv_obj_set_style_text_font(label, font, 0);
    lv_label_set_text(label, icon);
}
}  // namespace standalone::icons
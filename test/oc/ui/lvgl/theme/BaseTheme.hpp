#pragma once

// Minimal base-theme constants for native UI projection tests.

#include <cstdint>

namespace oc::ui::lvgl::base_theme::color {

inline constexpr uint32_t MACRO_1_RED = 0xCC7777;
inline constexpr uint32_t MACRO_2_ORANGE = 0xD1A35A;
inline constexpr uint32_t MACRO_3_YELLOW = 0xD3B16E;
inline constexpr uint32_t MACRO_4_GREEN = 0x77CC77;
inline constexpr uint32_t MACRO_5_CYAN = 0x6EAD9A;
inline constexpr uint32_t MACRO_6_BLUE = 0x7FA7C7;
inline constexpr uint32_t MACRO_7_PURPLE = 0x9D8BC0;
inline constexpr uint32_t MACRO_8_PINK = 0xB886A8;

inline constexpr uint32_t BACKGROUND = 0x101010;
inline constexpr uint32_t INACTIVE = 0x404040;
inline constexpr uint32_t ACTIVE = 0xFFFFFF;
inline constexpr uint32_t TEXT_PRIMARY = 0xFFFFFF;
inline constexpr uint32_t TEXT_SECONDARY = 0xC0C0C0;
inline constexpr uint32_t KNOB_BACKGROUND = 0x202020;
inline constexpr uint32_t KNOB_VALUE = 0xFFFFFF;
inline constexpr uint32_t KNOB_TRACK = 0x606060;

constexpr uint32_t getMacroColor(uint8_t index) {
    constexpr uint32_t colors[] = {
        MACRO_1_RED,
        MACRO_2_ORANGE,
        MACRO_3_YELLOW,
        MACRO_4_GREEN,
        MACRO_5_CYAN,
        MACRO_6_BLUE,
        MACRO_7_PURPLE,
        MACRO_8_PINK,
    };
    return colors[index % 8];
}

}  // namespace oc::ui::lvgl::base_theme::color

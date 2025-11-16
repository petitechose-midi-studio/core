#pragma once

#include <cstdint>

/**
 * @brief Base theme colors for common UI elements
 *
 * Standard colors used across DAW integrations.
 * These colors are commonly used in Bitwig, Ableton, and other DAWs
 * for parameter grouping and visual organization.
 */
namespace BaseTheme {
namespace Color {

constexpr uint32_t MACRO_1_RED = 0xF41B3E;
constexpr uint32_t MACRO_2_ORANGE = 0xFF7F17;
constexpr uint32_t MACRO_3_YELLOW = 0xFCEB23;
constexpr uint32_t MACRO_4_GREEN = 0x5BC515;
constexpr uint32_t MACRO_5_CYAN = 0x65CE92;
constexpr uint32_t MACRO_6_BLUE = 0x5CA6EE;
constexpr uint32_t MACRO_7_PURPLE = 0xC36EFF;
constexpr uint32_t MACRO_8_PINK = 0xFF54B0;

constexpr uint32_t MACROS[8] = {MACRO_1_RED,
                                MACRO_2_ORANGE,
                                MACRO_3_YELLOW,
                                MACRO_4_GREEN,
                                MACRO_5_CYAN,
                                MACRO_6_BLUE,
                                MACRO_7_PURPLE,
                                MACRO_8_PINK};

constexpr uint32_t BACKGROUND = 0x000000;
constexpr uint32_t INACTIVE = 0x333333;
constexpr uint32_t INACTIVE_LIGHTER = 0x666666;
constexpr uint32_t ACTIVE = 0xECA747;
constexpr uint32_t TEXT_PRIMARY = 0xFFFFFF;
constexpr uint32_t TEXT_PRIMARY_INVERTED = 0x292929;
constexpr uint32_t TEXT_SECONDARY = 0xD9D9D9;

constexpr uint32_t STATUS_SUCCESS = 0x00FF00;
constexpr uint32_t STATUS_WARNING = 0xFFA500;
constexpr uint32_t STATUS_ERROR = 0xFF0000;
constexpr uint32_t STATUS_INACTIVE = 0x606060;

constexpr uint32_t KNOB_BACKGROUND = 0x202020;
constexpr uint32_t KNOB_VALUE = 0x909090;
constexpr uint32_t KNOB_TRACK = 0x606060;

/**
 * @brief Get macro color by index (0-7)
 * @param index Macro index (0-7)
 * @return Color value or default if index out of range
 */
inline uint32_t getMacroColor(uint8_t index) {
    return (index < 8) ? MACROS[index] : INACTIVE;
}

}  // namespace Color
}  // namespace BaseTheme
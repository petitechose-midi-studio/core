#pragma once

#include <cstdint>
#include <oc/hal/Types.hpp>

namespace Config {

// ═══════════════════════════════════════════════════════════════════════════
// Button IDs
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Button identifiers for midi-studio hardware
 */
enum class ButtonID : oc::hal::ButtonID {
    // Left side navigation (10-19)
    LEFT_TOP = 10,
    LEFT_CENTER = 11,
    LEFT_BOTTOM = 12,

    // Bottom navigation (20-29)
    BOTTOM_LEFT = 20,
    BOTTOM_CENTER = 21,
    BOTTOM_RIGHT = 22,

    // Encoder integrated buttons (30-39)
    MACRO_1 = 31,
    MACRO_2 = 32,
    MACRO_3 = 33,
    MACRO_4 = 34,
    MACRO_5 = 35,
    MACRO_6 = 36,
    MACRO_7 = 37,
    MACRO_8 = 38,

    // Special encoder buttons (40-49)
    NAV = 40,
};

// ═══════════════════════════════════════════════════════════════════════════
// Encoder IDs
// ═══════════════════════════════════════════════════════════════════════════

/**
 * @brief Encoder identifiers for midi-studio hardware
 */
enum class EncoderID : oc::hal::EncoderID {
    // Main encoder matrix (301-308)
    MACRO_1 = 301,
    MACRO_2 = 302,
    MACRO_3 = 303,
    MACRO_4 = 304,
    MACRO_5 = 305,
    MACRO_6 = 306,
    MACRO_7 = 307,
    MACRO_8 = 308,

    // Special encoders (400-499)
    NAV = 400,
    OPT = 410,
};

}  // namespace Config

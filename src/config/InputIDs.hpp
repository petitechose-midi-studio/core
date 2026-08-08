#pragma once

/**
 * @file InputIDs.hpp
 * @brief Input device ID definitions
 */

#include <array>
#include <cstdint>
#include <ms/device_support/v1/InputIds.hpp>
#include <oc/type/Ids.hpp>

namespace Config {

// ═══════════════════════════════════════════════════════════════════════════
// Button IDs
// ═══════════════════════════════════════════════════════════════════════════

using ButtonID = ms::device_support::v1::ButtonID;

// ═══════════════════════════════════════════════════════════════════════════
// Encoder IDs
// ═══════════════════════════════════════════════════════════════════════════

using EncoderID = ms::device_support::v1::EncoderID;

// ═══════════════════════════════════════════════════════════════════════════
// Macro Encoder Mapping
// ═══════════════════════════════════════════════════════════════════════════

/// Number of macro encoders
inline constexpr uint8_t MACRO_COUNT =
    static_cast<uint8_t>(ms::device_support::v1::MACRO_COUNT);

/// Mapping from macro index (0-7) to encoder ID
inline constexpr std::array<EncoderID, MACRO_COUNT> MACRO_ENCODERS = {
    EncoderID::MACRO_1, EncoderID::MACRO_2,
    EncoderID::MACRO_3, EncoderID::MACRO_4,
    EncoderID::MACRO_5, EncoderID::MACRO_6,
    EncoderID::MACRO_7, EncoderID::MACRO_8
};

/// Mapping from macro index (0-7) to button ID
inline constexpr std::array<ButtonID, MACRO_COUNT> MACRO_BUTTONS = {
    ButtonID::MACRO_1, ButtonID::MACRO_2,
    ButtonID::MACRO_3, ButtonID::MACRO_4,
    ButtonID::MACRO_5, ButtonID::MACRO_6,
    ButtonID::MACRO_7, ButtonID::MACRO_8
};

inline bool macroEncoderIndex(oc::type::EncoderID id, uint8_t& outIndex) {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        if (static_cast<oc::type::EncoderID>(MACRO_ENCODERS[i]) == id) {
            outIndex = i;
            return true;
        }
    }
    return false;
}

inline bool macroButtonIndex(oc::type::ButtonID id, uint8_t& outIndex) {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        if (static_cast<oc::type::ButtonID>(MACRO_BUTTONS[i]) == id) {
            outIndex = i;
            return true;
        }
    }
    return false;
}

}  // namespace Config

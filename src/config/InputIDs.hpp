#pragma once

/**
 * @file InputIDs.hpp
 * @brief Input device ID definitions
 */

#include <cstdint>
#include <ms/device_support/v1/ControlLayout.hpp>
#include <ms/device_support/v1/InputIds.hpp>

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

using ms::device_support::v1::control::MACRO_BUTTONS;
using ms::device_support::v1::control::MACRO_ENCODERS;
using ms::device_support::v1::control::macroButtonIndex;
using ms::device_support::v1::control::macroEncoderIndex;

}  // namespace Config

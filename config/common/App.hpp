#pragma once

/**
 * @file App.hpp
 * @brief Application configuration constants
 *
 * Platform-agnostic configuration. Does not depend on hardware-specific headers.
 */

#include "Version.hpp"
#include "InputIDs.hpp"

#include <cstdint>

#include <oc/core/input/InputConfig.hpp>

namespace Config {

// ═══════════════════════════════════════════════════════════════════════════
// Application Info
// ═══════════════════════════════════════════════════════════════════════════

namespace App {
constexpr const char* NAME = "Midi Studio";
using Core::VERSION;
}  // namespace App

// ═══════════════════════════════════════════════════════════════════════════
// Context IDs
// ═══════════════════════════════════════════════════════════════════════════

enum class ContextID : uint8_t {
    STANDALONE = 0,  // First for debug (skipping BootContext)
    BOOT = 1,
};

// ═══════════════════════════════════════════════════════════════════════════
// Timing
// ═══════════════════════════════════════════════════════════════════════════

namespace Timing {
constexpr uint16_t REFRESH_HZ = 240;  // Display refresh rate (shared default)
constexpr uint32_t APP_HZ = REFRESH_HZ * 2;   // App polling rate (encoders, buttons)
constexpr uint32_t LVGL_HZ = REFRESH_HZ;      // Display refresh rate

constexpr uint8_t DEBOUNCE_MS = 12;  // Button debounce
constexpr uint32_t LONG_PRESS_MS = 500;
constexpr uint32_t LATCH_THRESHOLD_MS = 200;
constexpr uint32_t DOUBLE_TAP_MS = 300;     // Double tap window
}  // namespace Timing

// ═══════════════════════════════════════════════════════════════════════════
// Input Configuration
// ═══════════════════════════════════════════════════════════════════════════

namespace Input {
constexpr oc::core::InputConfig CONFIG = {.longPressMs = Timing::LONG_PRESS_MS,
                                          .doubleTapWindowMs = Timing::DOUBLE_TAP_MS,
                                          .latchThresholdMs = Timing::LATCH_THRESHOLD_MS,
                                          .debounceMs = Timing::DEBOUNCE_MS};
}

// ═══════════════════════════════════════════════════════════════════════════
// MIDI / SysEx
// ═══════════════════════════════════════════════════════════════════════════

namespace Midi {
/// USB MIDI SysEx buffer size (used by sysex_patch.py to patch Teensyduino)
constexpr uint16_t USB_SYSEX_MAX_SIZE = 16000;
}  // namespace Midi

}  // namespace Config

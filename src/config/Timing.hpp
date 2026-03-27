#pragma once

#include <cstdint>

namespace Config::Timing {

constexpr uint16_t REFRESH_HZ = 120;            // Display refresh rate
constexpr uint32_t APP_HZ = REFRESH_HZ * 2;     // App polling rate (encoders, buttons)
constexpr uint32_t LVGL_HZ = REFRESH_HZ;        // LVGL refresh cadence

constexpr uint8_t DEBOUNCE_MS = 12;             // Button debounce
constexpr uint32_t LONG_PRESS_MS = 500;
constexpr uint32_t OVERLAY_OPEN_LONG_PRESS_MS = 1000;
constexpr uint32_t LATCH_THRESHOLD_MS = 200;
constexpr uint32_t DOUBLE_TAP_MS = 300;         // Double tap window

}  // namespace Config::Timing

#pragma once

#include <cstdint>

namespace Config::Timing {

constexpr uint16_t REFRESH_HZ = 240;            // Display refresh rate
constexpr uint32_t APP_HZ = REFRESH_HZ * 8;     // App polling rate (encoders, buttons)
constexpr uint32_t LVGL_HZ = REFRESH_HZ;        // LVGL refresh cadence

constexpr uint8_t DEBOUNCE_MS = 12;             // Button debounce
constexpr uint32_t LONG_PRESS_MS = 500;
constexpr uint32_t OVERLAY_OPEN_LONG_PRESS_MS = 1000;
constexpr uint32_t LATCH_THRESHOLD_MS = 200;
constexpr uint32_t DOUBLE_TAP_MS = 300;         // Double tap window
constexpr uint32_t CONTEXT_CANCELLED_FEEDBACK_MS = 700;
constexpr uint32_t CONTEXT_APPLIED_FEEDBACK_MS = 1200;
// A short trailing-edge delay keeps preset focus responsive while avoiding
// repeated storage reads for every detent during a fast NAV sweep.
constexpr uint32_t PRESET_LIBRARY_INSPECTION_SETTLE_MS = 40;
constexpr uint32_t STATUS_MIDI_PULSE_MS = 80;   // Transport MIDI/clock pulse window
constexpr uint32_t STATUS_BEAT_PULSE_MS = 100;  // Transport beat pulse window

}  // namespace Config::Timing

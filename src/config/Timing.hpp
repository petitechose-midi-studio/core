#pragma once

#include <cstdint>

namespace Config::Timing {

// These rates have independent owners. Their current numeric relationship is
// a product baseline, not a derivation contract between input, UI and display.
constexpr uint32_t INPUT_APP_ADMISSION_HZ = 1'920U;
constexpr uint32_t LVGL_SERVICE_HZ = 240U;
constexpr uint32_t RETAINED_VIEW_HZ = 240U;
constexpr uint32_t PHYSICAL_DISPLAY_REQUEST_HZ = 240U;

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

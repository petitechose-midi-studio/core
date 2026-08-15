#pragma once

#include <cstdint>
#include <ms/device_support/v1/Timing.hpp>

namespace Config::Timing {

// These rates have independent owners. Their current numeric relationship is
// a product baseline, not a derivation contract between input, UI and display.
constexpr uint32_t INPUT_APP_ADMISSION_HZ =
    ms::device_support::v1::timing::INPUT_APP_ADMISSION_HZ;
constexpr uint32_t LVGL_SERVICE_HZ =
    ms::device_support::v1::timing::LVGL_SERVICE_HZ;
// LVGL timers use whole milliseconds. A 5 ms period is therefore the honest
// retained-view contract: 200 Hz, rather than a nominal 240 Hz rounded to the
// same 5 ms period at every call site.
constexpr uint32_t RETAINED_VIEW_PERIOD_MS = 5U;
constexpr uint32_t PHYSICAL_DISPLAY_REQUEST_HZ =
    ms::device_support::v1::timing::PHYSICAL_DISPLAY_REQUEST_HZ;

constexpr uint8_t DEBOUNCE_MS = static_cast<uint8_t>(
    ms::device_support::v1::timing::DEBOUNCE_MS);  // Button debounce
constexpr uint32_t LONG_PRESS_MS =
    ms::device_support::v1::timing::LONG_PRESS_MS;
constexpr uint32_t OVERLAY_OPEN_LONG_PRESS_MS = 1000;
constexpr uint32_t LATCH_THRESHOLD_MS =
    ms::device_support::v1::timing::LATCH_THRESHOLD_MS;
constexpr uint32_t DOUBLE_TAP_MS =
    ms::device_support::v1::timing::DOUBLE_TAP_MS;  // Double tap window
constexpr uint32_t CONTEXT_CANCELLED_FEEDBACK_MS = 700;
constexpr uint32_t CONTEXT_APPLIED_FEEDBACK_MS = 1200;
// A short trailing-edge delay keeps preset focus responsive while avoiding
// repeated storage reads for every detent during a fast NAV sweep.
constexpr uint32_t PRESET_LIBRARY_INSPECTION_SETTLE_MS = 40;
constexpr uint32_t STATUS_MIDI_PULSE_MS = 80;   // Transport MIDI/clock pulse window
constexpr uint32_t STATUS_BEAT_PULSE_MS = 100;  // Transport beat pulse window

}  // namespace Config::Timing

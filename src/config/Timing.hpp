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
constexpr uint32_t LVGL_SERVICE_PERIOD_US = 1000000U / LVGL_SERVICE_HZ;
constexpr uint32_t UI_FRAME_SERVICE_DIVISOR =
    ms::device_support::v1::timing::UI_FRAME_SERVICE_DIVISOR;
constexpr uint32_t UI_FRAME_HZ =
    ms::device_support::v1::timing::UI_FRAME_HZ;
constexpr uint32_t UI_FRAME_PERIOD_US =
    LVGL_SERVICE_PERIOD_US * UI_FRAME_SERVICE_DIVISOR;
// LVGL timers use whole milliseconds. Servicing this 8 ms timer from the
// 240 Hz lane admits at most one dirty projection every second service pass.
constexpr uint32_t UI_FRAME_PERIOD_MS = 1000U / UI_FRAME_HZ;
static_assert(UI_FRAME_PERIOD_US > 0U);
static_assert(UI_FRAME_PERIOD_MS > 0U);
// The internal timer lane owns musical scheduling at 1 kHz. The foreground
// control plane keeps the same admission rate for transport and activation
// acknowledgements, but large mutable authoring snapshots are paced below.
constexpr uint32_t SEQUENCER_REALTIME_HZ = 1000U;
constexpr uint32_t SEQUENCER_REALTIME_PERIOD_US =
    1000000U / SEQUENCER_REALTIME_HZ;
// Lower Teensy IRQ values have higher priority. Display work deliberately runs
// below this timer so visual load cannot preempt musical scheduling.
constexpr uint8_t SEQUENCER_REALTIME_IRQ_PRIORITY =
    ms::device_support::v1::timing::MUSICAL_REALTIME_IRQ_PRIORITY;
static_assert(SEQUENCER_REALTIME_PERIOD_US > 0U);
// Authoring edits only need to reach the already-running timer once per UI
// service period. This coalesces encoder bursts without changing the 1 kHz
// musical scheduler or adding more than one 240 Hz frame of edit latency.
constexpr uint32_t SEQUENCER_AUTHORING_PUBLICATION_HZ = LVGL_SERVICE_HZ;
constexpr uint32_t SEQUENCER_AUTHORING_PUBLICATION_PERIOD_US =
    1000000U / SEQUENCER_AUTHORING_PUBLICATION_HZ;
static_assert(SEQUENCER_AUTHORING_PUBLICATION_PERIOD_US >=
              SEQUENCER_REALTIME_PERIOD_US);

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

}  // namespace Config::Timing

/*
 * System.hpp
 *
 * System-wide configuration constants for Midi Studio.
 *
 * This file defines all compile-time constants for:
 * - Application metadata (name, version)
 * - Hardware specifications (pins, timing, debounce)
 * - Display settings (resolution, refresh rate, memory)
 * - MIDI parameters (SysEx buffer size)
 * - Input timing (gestures, debounce)
 *
 * All values are constexpr - they cannot be changed at runtime.
 * Modify these values to adapt the system to your hardware configuration.
 */

#pragma once

#include <cstddef>
#include <cstdint>

#include "Version.hpp"

namespace System {

/*
 * Application
 *
 * Application identification and version information.
 * Version numbers are imported from Version.hpp
 */
namespace Application {
constexpr const char* NAME = "Midi Studio";

// Import version from Core namespace
using Core::IS_PRERELEASE;
using Core::VERSION;
using Core::VERSION_MAJOR;
using Core::VERSION_MINOR;
using Core::VERSION_PATCH;
}  // namespace Application

/*
 * Hardware
 *
 * Physical hardware configuration: pin assignments and timing.
 * Defines the electrical interface between the microcontroller and peripherals.
 */
namespace Hardware {
/* Display pins (ILI9341 SPI interface) */
constexpr uint8_t DISPLAY_CS_PIN = 28;
constexpr uint8_t DISPLAY_DC_PIN = 0;
constexpr uint8_t DISPLAY_RST_PIN = 29;
constexpr uint8_t DISPLAY_MOSI_PIN = 26;
constexpr uint8_t DISPLAY_SCK_PIN = 27;
constexpr uint8_t DISPLAY_MISO_PIN = 1;
constexpr uint32_t DISPLAY_SPI_SPEED = 20000000;

/* Multiplexer pins (CD74HC4067) */
constexpr uint8_t MUX_S0_PIN = 3;
constexpr uint8_t MUX_S1_PIN = 2;
constexpr uint8_t MUX_S2_PIN = 5;
constexpr uint8_t MUX_S3_PIN = 6;
constexpr uint8_t MUX_SIGNAL_PIN = 4;
constexpr uint8_t MUX_MAX_CHANNELS = 16;

/* Input timing (debouncing) */
constexpr uint16_t MUX_DEBOUNCE_US = 20;    /* microseconds - mux channel switching settle time */
constexpr uint32_t PIN_DEBOUNCE_MS = 5;     /* milliseconds - direct pin debounce */
}  // namespace Hardware

/*
 * Display
 *
 * Display specifications, memory allocation, and refresh timing.
 * Controls rendering performance and memory usage.
 */
namespace Display {
/* Screen dimensions and orientation */
constexpr uint16_t SCREEN_WIDTH = 320;
constexpr uint16_t SCREEN_HEIGHT = 240;
constexpr uint8_t ROTATION = 3; /* ILI9341 driver rotation (0 = optimal performance) */

/* Memory buffers */
constexpr size_t FRAMEBUFFER_SIZE = SCREEN_WIDTH * SCREEN_HEIGHT;
constexpr size_t DIFFBUFFER_SIZE = 16384; /* 16KB for better diff precision (was 4KB) */
constexpr size_t LVGL_BUFFER_LINES = SCREEN_HEIGHT;
constexpr size_t LVGL_BUFFER_SIZE = SCREEN_WIDTH * LVGL_BUFFER_LINES;

/* Refresh timing */
constexpr int REFRESH_RATE_HZ = 60;
constexpr uint32_t REFRESH_PERIOD_MS = (1000 / REFRESH_RATE_HZ);

/* VSync timing */
constexpr int VSYNC_SPACING = 2;
constexpr int VSYNC_RATE_HZ = REFRESH_RATE_HZ / VSYNC_SPACING;
constexpr unsigned long VSYNC_PERIOD_MS = REFRESH_PERIOD_MS * VSYNC_SPACING;

/* Advanced display options */
constexpr int DIFF_GAP = 4;
constexpr int IRQ_PRIORITY = 128;
constexpr float LATE_START_RATIO = 0.3f;
}  // namespace Display

/*
 * Midi
 *
 * MIDI protocol parameters.
 */
namespace Midi {
constexpr size_t MAX_ACTIVE_NOTES = 16;

/* USB MIDI SysEx buffer size
 * Maximum size of SysEx messages that can be received/sent via USB MIDI.
 * Default Teensy value is 290 bytes.
 * NOTE: This value is automatically injected into the Teensy framework at build time.
 */
constexpr size_t USB_SYSEX_MAX_SIZE = 16000;
}  // namespace Midi

/*
 * Input
 *
 * Input binding timing and behavior.
 * Defines timing thresholds for button gestures.
 */
namespace Input {
constexpr uint32_t LONG_PRESS_DEFAULT_MS = 500;  /* milliseconds */
constexpr uint32_t DOUBLE_TAP_WINDOW_MS = 300;   /* milliseconds */
constexpr uint32_t LATCH_THRESHOLD_MS = 300;     /* milliseconds - tap shorter = latch, longer = momentary */
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;      /* milliseconds - software debounce for state changes */
}  // namespace Input

}  // namespace System

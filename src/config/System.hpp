/*
 * System.hpp
 *
 * System-wide configuration constants for Midi Studio.
 *
 * This file defines all compile-time constants for:
 * - Application metadata (name, version)
 * - Hardware specifications (pins, component counts, timing)
 * - Display settings (resolution, refresh rate, memory)
 * - MIDI parameters (channels, CC ranges, rate limiting)
 * - UI behavior (debug mode, colors)
 * - Memory limits (event system, MIDI queues, UI components)
 *
 * All values are constexpr - they cannot be changed at runtime.
 * Modify these values to adapt the system to your hardware configuration.
 */

#pragma once

#include <climits>
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
using Core::VERSION;
using Core::VERSION_MAJOR;
using Core::VERSION_MINOR;
using Core::VERSION_PATCH;
using Core::IS_PRERELEASE;
}  // namespace Application

/*
 * Hardware
 *
 * Physical hardware configuration: pin assignments, component counts, timing.
 * Defines the electrical interface between the microcontroller and peripherals.
 */
namespace Hardware {
/* Input device counts */
constexpr size_t ENCODERS_COUNT = 10;
constexpr size_t BUTTONS_COUNT = 15;

/* Display pins (ILI9341 SPI interface) */
constexpr uint8_t DISPLAY_CS_PIN = 28;
constexpr uint8_t DISPLAY_DC_PIN = 0;
constexpr uint8_t DISPLAY_RST_PIN = 29;
constexpr uint8_t DISPLAY_MOSI_PIN = 26;
constexpr uint8_t DISPLAY_SCK_PIN = 27;
constexpr uint8_t DISPLAY_MISO_PIN = 1;
constexpr uint32_t DISPLAY_SPI_SPEED = 70000000; /* 70 MHz */

/* Multiplexer pins (CD74HC4067) */
constexpr uint8_t MUX_S0_PIN = 3;
constexpr uint8_t MUX_S1_PIN = 2;
constexpr uint8_t MUX_S2_PIN = 5;
constexpr uint8_t MUX_S3_PIN = 6;
constexpr uint8_t MUX_SIGNAL_PIN = 4;
constexpr uint8_t MUX_MAX_CHANNELS = 16;

/* Input timing (debouncing) */
constexpr uint16_t BUTTON_DEBOUNCE_US = 20; /* microseconds */
constexpr uint16_t MUX_DEBOUNCE_US = BUTTON_DEBOUNCE_US;
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
constexpr int REFRESH_RATE_HZ = 200;
constexpr uint32_t REFRESH_PERIOD_MS = (1000 / REFRESH_RATE_HZ);

/* VSync timing */
constexpr int VSYNC_SPACING = 2;
constexpr int VSYNC_RATE_HZ = REFRESH_RATE_HZ / VSYNC_SPACING;
constexpr unsigned long VSYNC_PERIOD_MS = REFRESH_PERIOD_MS * VSYNC_SPACING;

/* Advanced display options */
constexpr int DIFF_GAP = 4;
constexpr int IRQ_PRIORITY = 128;
constexpr float LATE_START_RATIO = 0.1f;
}  // namespace Display

/*
 * Midi
 *
 * MIDI protocol parameters and rate limiting.
 * Defines MIDI channel defaults, value ranges, and timing constraints.
 */
namespace Midi {
constexpr uint8_t DEFAULT_CHANNEL = 0;
constexpr uint8_t CC_VALUE_MIN = 0;
constexpr uint8_t CC_VALUE_MAX = 127;
constexpr size_t MAX_ACTIVE_NOTES = 16;

/* Rate limiting (prevent MIDI flooding) */
constexpr unsigned long DUPLICATE_CHECK_MS = 1.5; /* milliseconds */
constexpr unsigned long ENCODER_RATE_LIMIT_MS = 5;

/* USB MIDI SysEx buffer size
 * Maximum size of SysEx messages that can be received/sent via USB MIDI.
 * Default Teensy value is 290 bytes.
 * Increase this if you need to handle larger SysEx messages.
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
constexpr uint32_t BUTTON_DEBOUNCE_MS = 50;      /* milliseconds - software debounce for state changes */
}  // namespace Input

/*
 * UI
 *
 * User interface behavior and appearance.
 */
namespace UI {
constexpr bool SHOW_DEBUG_INFO = false;
constexpr bool ENABLE_FULL_UI = true;

/* Basic colors */
constexpr uint32_t COLOR_BLACK = 0x000000;
constexpr uint32_t COLOR_WHITE = 0xFFFFFF;
}  // namespace UI

/*
 * Memory
 *
 * Static memory allocation limits for embedded containers.
 * These define maximum capacities for etl::vector, etl::map, etc.
 *
 * Increase these values if you encounter container overflow errors.
 * Decreasing these values reduces RAM usage.
 */
namespace Memory {
/* Input system */
constexpr size_t MAX_CONTROL_DEFINITIONS = Hardware::ENCODERS_COUNT + Hardware::BUTTONS_COUNT;
constexpr size_t MAX_MIDI_MAPPINGS = MAX_CONTROL_DEFINITIONS;

/* Event system */
constexpr size_t MAX_EVENT_SUBSCRIBERS = 32;
constexpr size_t MAX_EVENT_TYPES = 96;
constexpr size_t MAX_CALLBACKS_PER_EVENT = 16;

/* MIDI system */
constexpr size_t MAX_MIDI_CALLBACKS = MAX_CONTROL_DEFINITIONS;
constexpr size_t MAX_MIDI_PENDING_PARAMS = MAX_CONTROL_DEFINITIONS;
constexpr size_t MAX_MIDI_MESSAGES_QUEUE = 32;

/* UI system */
constexpr size_t MAX_NAVIGATION_ACTIONS = 32;
constexpr size_t MAX_UI_COMPONENTS = 16;

/* Task scheduler */
constexpr size_t MAX_SCHEDULED_TASKS = 8;
}  // namespace Memory

}  // namespace System

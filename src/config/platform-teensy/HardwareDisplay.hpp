#pragma once

/**
 * @file HardwareDisplay.hpp
 * @brief Display hardware configuration
 *
 * Platform-conditional: Teensy-specific config only included when not desktop.
 */

#include <cstdint>

#include <config/Timing.hpp>

// Only include Teensy headers when building for Teensy (not SDL desktop)
#ifndef OC_DESKTOP
#include <oc/hal/teensy/Ili9341.hpp>
#include <oc/ui/lvgl/Bridge.hpp>
#endif

namespace Hardware {
namespace Display {

// ═══════════════════════════════════════════════════════════════════════════
// Common constants (shared between Teensy and Desktop)
// ═══════════════════════════════════════════════════════════════════════════

constexpr uint8_t VSYNC_SPACING = 1;
constexpr uint32_t SPI_SPEED = 60'000'000;

static_assert(Config::Timing::LVGL_HZ > 0, "LVGL_HZ must be > 0");
static_assert((Config::Timing::LVGL_HZ % VSYNC_SPACING) == 0,
              "LVGL_HZ must be divisible by VSYNC_SPACING");

// ═══════════════════════════════════════════════════════════════════════════
// Teensy-specific configuration (only when building for Teensy)
// ═══════════════════════════════════════════════════════════════════════════

#ifndef OC_DESKTOP

constexpr oc::hal::teensy::Ili9341Config CONFIG = {
    .width = 320,
    .height = 240,
    .csPin = 28,
    .dcPin = 0,
    .rstPin = 29,
    .mosiPin = 26,
    .sckPin = 27,
    .misoPin = 1,
    .spiSpeed = SPI_SPEED,
    .rotation = 3,
    .invertDisplay = true,
    .vsyncSpacing = VSYNC_SPACING,
    .diffGap = 4,
    .irqPriority = 128,
    .lateStartRatio = 0.2f,
    .refreshRate = Config::Timing::LVGL_HZ
};

constexpr size_t BUFFER_SIZE = CONFIG.framebufferSize();  // 320*240 = 76800
constexpr size_t DIFF_SIZE = 16384;  // 8KB

#endif // !OC_DESKTOP

}  // namespace Display

// ═══════════════════════════════════════════════════════════════════════════
// LVGL Bridge configuration (Teensy only - SDL uses SdlBridge)
// ═══════════════════════════════════════════════════════════════════════════

#ifndef OC_DESKTOP
namespace LVGL {
constexpr oc::ui::lvgl::BridgeConfig CONFIG = {
    // DIRECT mode draws only invalid areas into the framebuffer while keeping
    // full-frame buffers for the ILI9341 diff transfer path.
    .renderMode = LV_DISPLAY_RENDER_MODE_DIRECT,
    .buffer2 = nullptr,
    .refreshHz = Config::Timing::LVGL_HZ
};
}  // namespace LVGL
#endif // !OC_DESKTOP

}  // namespace Hardware

#pragma once

/**
 * @file HardwareDisplay.hpp
 * @brief Display hardware configuration
 */

#include <oc/teensy/Ili9341.hpp>
#include <oc/ui/lvgl/Bridge.hpp>

namespace Hardware {
namespace Display {

// Display settings optimized for performance
constexpr uint8_t VSYNC_SPACING = 1;
constexpr uint32_t SPI_SPEED = 40'000'000;  // 20MHz
constexpr uint16_t REFRESH_HZ = 240;  // 200Hz display rate

constexpr oc::teensy::Ili9341Config CONFIG = {
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
    .refreshRate = REFRESH_HZ
};

constexpr size_t BUFFER_SIZE = CONFIG.framebufferSize();  // 320*240 = 76800
constexpr size_t DIFF_SIZE = 16384;  // 8KB

}  // namespace Display

namespace LVGL {
constexpr oc::ui::lvgl::BridgeConfig CONFIG = {
    .renderMode = LV_DISPLAY_RENDER_MODE_FULL,
    .buffer2 = nullptr,
    .refreshHz = Display::REFRESH_HZ / Display::VSYNC_SPACING
};
}  // namespace LVGL

}  // namespace Hardware

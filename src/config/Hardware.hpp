#pragma once

#include <oc/teensy/Ili9341.hpp>
#include <oc/ui/lvgl/Bridge.hpp>

#include "App.hpp"

namespace Hardware {

namespace Display {
constexpr uint8_t VSYNC_SPACING = 2;

constexpr oc::teensy::Ili9341Config CONFIG = {
    .width = 320,
    .height = 240,
    .csPin = 28,
    .dcPin = 0,
    .rstPin = 29,
    .mosiPin = 26,
    .sckPin = 27,
    .misoPin = 1,
    .spiSpeed = 20'000'000,
    .rotation = 3,
    .invertDisplay = false,
    .vsyncSpacing = VSYNC_SPACING,
    .diffGap = 4,
    .irqPriority = 128,
    .lateStartRatio = 0.3f,
    .refreshRate = Config::Timing::LVGL_HZ * VSYNC_SPACING
};

constexpr size_t BUFFER_SIZE = CONFIG.framebufferSize();  // 320*240 = 76800
constexpr size_t DIFF_SIZE = 16384;  // 16KB
}

namespace LVGL {
constexpr oc::ui::lvgl::BridgeConfig CONFIG = {
    .renderMode = LV_DISPLAY_RENDER_MODE_FULL,
    .buffer2 = nullptr,
    .refreshHz = Config::Timing::LVGL_HZ
};
}

}  // namespace Hardware

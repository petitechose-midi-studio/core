#pragma once

#include <cstddef>
#include <cstdint>

#include <ms/device_support/v1/Timing.hpp>

#include <oc/hal/teensy/Ili9341.hpp>
#include <oc/ui/lvgl/Bridge.hpp>

namespace ms::device_support::v1::display {

inline constexpr std::uint8_t VSYNC_SPACING = 1;
inline constexpr std::uint32_t SPI_SPEED_HZ = 50'000'000;

static_assert(timing::LVGL_SERVICE_HZ > 0);
static_assert(timing::LVGL_SERVICE_HZ % VSYNC_SPACING == 0);
static_assert(
    timing::LVGL_SERVICE_HZ == timing::PHYSICAL_DISPLAY_REQUEST_HZ);

inline constexpr oc::hal::teensy::Ili9341Config CONFIG{
    320,
    240,
    28,
    0,
    29,
    26,
    27,
    1,
    SPI_SPEED_HZ,
    3,
    true,
    VSYNC_SPACING,
    4,
    128,
    0.2f,
    timing::PHYSICAL_DISPLAY_REQUEST_HZ,
};

inline constexpr std::size_t FRAMEBUFFER_PIXEL_COUNT =
    CONFIG.framebufferSize();
inline constexpr std::size_t DIFF_BUFFER_SIZE_BYTES = 16'384;

inline constexpr oc::ui::lvgl::BridgeConfig LVGL_CONFIG{
    LV_DISPLAY_RENDER_MODE_DIRECT,
    nullptr,
    timing::LVGL_SERVICE_HZ,
    {},
};

}  // namespace ms::device_support::v1::display

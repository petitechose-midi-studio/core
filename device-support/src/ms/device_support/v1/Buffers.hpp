#pragma once

#include <cstdint>

#include <ms/device_support/v1/Display.hpp>

#include <config/PlatformCompat.hpp>

namespace ms::device_support::v1::buffers {

inline EXTMEM std::uint16_t framebuffer[display::FRAMEBUFFER_PIXEL_COUNT];
inline EXTMEM std::uint8_t diff1[display::DIFF_BUFFER_SIZE_BYTES];
inline EXTMEM std::uint8_t diff2[display::DIFF_BUFFER_SIZE_BYTES];
inline DMAMEM std::uint16_t lvgl[display::FRAMEBUFFER_PIXEL_COUNT];

}  // namespace ms::device_support::v1::buffers

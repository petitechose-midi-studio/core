#pragma once

/**
 * @file Buffer.hpp
 * @brief Buffer size configuration
 */

#include <config/PlatformCompat.hpp>
#include <lvgl.h>

#include "Hardware.hpp"

namespace Buffer {

inline EXTMEM uint16_t framebuffer[Hardware::Display::BUFFER_SIZE];
inline EXTMEM uint8_t diff1[Hardware::Display::DIFF_SIZE];
inline EXTMEM uint8_t diff2[Hardware::Display::DIFF_SIZE];
inline DMAMEM uint16_t lvgl[Hardware::Display::BUFFER_SIZE];

}  // namespace Buffer

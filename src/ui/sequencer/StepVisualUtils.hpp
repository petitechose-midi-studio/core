#pragma once

/**
 * @file StepVisualUtils.hpp
 * @brief Reusable mapping helpers for sequencer step visuals.
 */

#include <cstdint>

#include <lvgl.h>

namespace core::ui::sequencer::visual {

inline uint8_t mapToRangeU8(uint16_t value, uint16_t inMax, uint8_t outMin, uint8_t outMax) {
    if (outMax <= outMin) return outMin;
    if (inMax == 0) return outMin;
    if (value > inMax) value = inMax;

    const uint16_t outSpan = static_cast<uint16_t>(outMax - outMin);
    const uint32_t scaled = static_cast<uint32_t>(value) * static_cast<uint32_t>(outSpan)
        + static_cast<uint32_t>(inMax / 2U);
    return static_cast<uint8_t>(outMin + static_cast<uint8_t>(scaled / inMax));
}

inline lv_color_t grayscaleColor(uint8_t brightnessPct) {
    if (brightnessPct > 100) brightnessPct = 100;
    return lv_color_hsv_to_rgb(0, 0, brightnessPct);
}

}  // namespace core::ui::sequencer::visual

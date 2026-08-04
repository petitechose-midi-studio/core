#pragma once

#include <cstdint>

enum lv_display_render_mode_t : std::uint8_t {
    LV_DISPLAY_RENDER_MODE_PARTIAL = 0,
    LV_DISPLAY_RENDER_MODE_DIRECT = 1,
    LV_DISPLAY_RENDER_MODE_FULL = 2,
};

struct lv_color_t {
    std::uint16_t full = 0;
};

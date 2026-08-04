#pragma once

#include <cstdint>

#include <lvgl.h>

namespace oc::ui::lvgl {

struct BridgeConfig {
    lv_display_render_mode_t renderMode = LV_DISPLAY_RENDER_MODE_FULL;
    void* buffer2 = nullptr;
    std::uint32_t refreshHz = 0;
    lv_color_t screenBgColor{};
};

}  // namespace oc::ui::lvgl

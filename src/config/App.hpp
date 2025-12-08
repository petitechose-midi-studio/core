#pragma once

#include <cstdint>

namespace Config {

// Source unique pour le framerate - utilisé partout
namespace Timing {
constexpr uint32_t LVGL_HZ = 60;
constexpr uint32_t LVGL_PERIOD_MS = 1000 / LVGL_HZ;
}

}  // namespace Config

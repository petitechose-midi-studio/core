#pragma once

#include <cstdint>

namespace Config {

// ═══════════════════════════════════════════════════════════════════════════
// Context IDs
// ═══════════════════════════════════════════════════════════════════════════

enum class ContextID : uint8_t {
    BOOT = 0,
    STANDALONE = 1,
};

// ═══════════════════════════════════════════════════════════════════════════
// Timing
// ═══════════════════════════════════════════════════════════════════════════

namespace Timing {
constexpr uint32_t APP_HZ = 1000;     // App polling rate (encoders, buttons)
constexpr uint32_t LVGL_HZ = 60;      // Display refresh rate
constexpr uint32_t LVGL_PERIOD_MS = 1000 / LVGL_HZ;
}

}  // namespace Config

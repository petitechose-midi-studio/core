#pragma once

#include <cstdint>

namespace ms::device_support::v1::timing {

inline constexpr std::uint32_t INPUT_APP_ADMISSION_HZ = 1'920;
inline constexpr std::uint32_t LVGL_SERVICE_HZ = 240;
inline constexpr std::uint32_t PHYSICAL_DISPLAY_REQUEST_HZ = 240;

inline constexpr std::uint32_t DEBOUNCE_MS = 12;
inline constexpr std::uint32_t LONG_PRESS_MS = 500;
inline constexpr std::uint32_t LATCH_THRESHOLD_MS = 200;
inline constexpr std::uint32_t DOUBLE_TAP_MS = 300;

static_assert(INPUT_APP_ADMISSION_HZ % LVGL_SERVICE_HZ == 0);

}  // namespace ms::device_support::v1::timing

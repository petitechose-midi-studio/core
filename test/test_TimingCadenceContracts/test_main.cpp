#include <cassert>
#include <cstdint>

#include <config/Timing.hpp>

namespace {

constexpr uint32_t MICROS_PER_SECOND = 1'000'000U;
constexpr uint32_t MILLIS_PER_SECOND = 1'000U;

}  // namespace

int main() {
    static_assert(Config::Timing::INPUT_APP_ADMISSION_HZ == 1'920U);
    static_assert(Config::Timing::LVGL_SERVICE_HZ == 240U);
    static_assert(Config::Timing::RETAINED_VIEW_PERIOD_MS == 5U);
    static_assert(Config::Timing::PHYSICAL_DISPLAY_REQUEST_HZ == 240U);

    static_assert(
        MICROS_PER_SECOND / Config::Timing::INPUT_APP_ADMISSION_HZ == 520U
    );
    static_assert(
        MICROS_PER_SECOND / Config::Timing::LVGL_SERVICE_HZ == 4'166U
    );
    static_assert(
        MILLIS_PER_SECOND / Config::Timing::RETAINED_VIEW_PERIOD_MS == 200U
    );

    assert(Config::Timing::INPUT_APP_ADMISSION_HZ == 1'920U);
    assert(Config::Timing::LVGL_SERVICE_HZ == 240U);
    assert(Config::Timing::RETAINED_VIEW_PERIOD_MS == 5U);
    assert(Config::Timing::PHYSICAL_DISPLAY_REQUEST_HZ == 240U);
    return 0;
}

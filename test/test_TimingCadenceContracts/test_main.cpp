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
    static_assert(Config::Timing::LVGL_SERVICE_PERIOD_US == 4'166U);
    static_assert(Config::Timing::UI_FRAME_SERVICE_DIVISOR == 2U);
    static_assert(Config::Timing::UI_FRAME_HZ == 120U);
    static_assert(Config::Timing::UI_FRAME_PERIOD_MS == 8U);
    static_assert(Config::Timing::UI_FRAME_PERIOD_US == 8'332U);

    static_assert(
        MICROS_PER_SECOND / Config::Timing::INPUT_APP_ADMISSION_HZ == 520U
    );
    static_assert(
        MICROS_PER_SECOND / Config::Timing::LVGL_SERVICE_HZ == 4'166U
    );
    static_assert(
        Config::Timing::UI_FRAME_PERIOD_MS >
            MILLIS_PER_SECOND / Config::Timing::LVGL_SERVICE_HZ
    );
    static_assert(
        Config::Timing::UI_FRAME_PERIOD_US ==
            Config::Timing::LVGL_SERVICE_PERIOD_US *
                Config::Timing::UI_FRAME_SERVICE_DIVISOR
    );

    assert(Config::Timing::INPUT_APP_ADMISSION_HZ == 1'920U);
    assert(Config::Timing::LVGL_SERVICE_HZ == 240U);
    assert(Config::Timing::UI_FRAME_SERVICE_DIVISOR == 2U);
    assert(Config::Timing::UI_FRAME_HZ == 120U);
    assert(Config::Timing::UI_FRAME_PERIOD_MS == 8U);
    assert(Config::Timing::UI_FRAME_PERIOD_US == 8'332U);
    return 0;
}

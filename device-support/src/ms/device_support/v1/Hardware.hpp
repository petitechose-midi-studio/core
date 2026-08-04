#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ms/device_support/v1/InputIds.hpp>
#include <ms/device_support/v1/Timing.hpp>

#include <oc/hal/common/embedded/ButtonDef.hpp>
#include <oc/hal/common/embedded/EncoderDef.hpp>
#include <oc/hal/common/embedded/GpioPin.hpp>
#include <oc/hal/teensy/GenericMux.hpp>

namespace ms::device_support::v1 {

namespace mux {

inline constexpr std::uint8_t BUTTON_READS_PER_APP_TICK = 1;
inline constexpr oc::hal::teensy::CD74HC4067::Config CONFIG{
    {3, 2, 5, 6},
    4,
    20,
    true,
};

}  // namespace mux

namespace encoder {

using oc::hal::common::embedded::EncoderDef;

inline constexpr std::uint16_t PPR = 24;
inline constexpr std::uint16_t RANGE = 270;
inline constexpr std::uint8_t TICKS = 1;
inline constexpr bool INVERT = true;

inline constexpr std::array<EncoderDef, 10> ENCODERS{
    EncoderDef(EncoderID::MACRO_1, 22, 23, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_2, 18, 19, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_3, 40, 41, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_4, 36, 37, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_5, 20, 21, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_6, 16, 17, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_7, 14, 15, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_8, 38, 39, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::NAV, 31, 30, 24, 270, 4, !INVERT),
    EncoderDef(EncoderID::OPT, 34, 33, 600, 270, 1, INVERT),
};

}  // namespace encoder

namespace button {

using oc::hal::common::embedded::ButtonDef;
using Source = oc::hal::common::embedded::GpioPin::Source;

inline constexpr std::array<ButtonDef, 15> BUTTONS{
    ButtonDef(ButtonID::LEFT_TOP, {9, Source::MUX}, true),
    ButtonDef(ButtonID::LEFT_CENTER, {10, Source::MUX}, true),
    ButtonDef(ButtonID::LEFT_BOTTOM, {11, Source::MUX}, true),

    ButtonDef(ButtonID::BOTTOM_LEFT, {14, Source::MUX}, true),
    ButtonDef(ButtonID::BOTTOM_CENTER, {13, Source::MUX}, true),
    ButtonDef(ButtonID::BOTTOM_RIGHT, {12, Source::MUX}, true),

    ButtonDef(ButtonID::NAV, {32, Source::MCU}, true),

    ButtonDef(ButtonID::MACRO_1, {7, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_2, {4, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_3, {2, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_4, {0, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_5, {6, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_6, {5, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_7, {3, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_8, {1, Source::MUX}, true),
};

inline constexpr std::size_t MUX_BUTTON_COUNT = []() constexpr {
    std::size_t count = 0;
    for (const auto& button : BUTTONS) {
        if (button.pin.source == Source::MUX) {
            ++count;
        }
    }
    return count;
}();

static_assert(mux::BUTTON_READS_PER_APP_TICK > 0);
inline constexpr std::uint32_t MUX_SCAN_TICKS =
    (MUX_BUTTON_COUNT + mux::BUTTON_READS_PER_APP_TICK - 1U) /
    mux::BUTTON_READS_PER_APP_TICK;
inline constexpr std::uint32_t MUX_SCAN_PERIOD_US =
    (MUX_SCAN_TICKS * 1'000'000U + timing::INPUT_APP_ADMISSION_HZ - 1U) /
    timing::INPUT_APP_ADMISSION_HZ;

static_assert(
    MUX_SCAN_PERIOD_US <= timing::DEBOUNCE_MS * 1'000U,
    "MUX button scan period must fit inside the debounce window");

}  // namespace button

}  // namespace ms::device_support::v1

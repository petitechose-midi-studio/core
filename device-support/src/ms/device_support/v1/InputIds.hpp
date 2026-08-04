#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/Config.hpp>
#include <oc/type/Ids.hpp>

namespace ms::device_support::v1 {

enum class ButtonID : oc::type::ButtonID {
    LEFT_TOP = 10,
    LEFT_CENTER = 11,
    LEFT_BOTTOM = 12,

    BOTTOM_LEFT = 20,
    BOTTOM_CENTER = 21,
    BOTTOM_RIGHT = 22,

    MACRO_1 = 31,
    MACRO_2 = 32,
    MACRO_3 = 33,
    MACRO_4 = 34,
    MACRO_5 = 35,
    MACRO_6 = 36,
    MACRO_7 = 37,
    MACRO_8 = 38,

    NAV = 40,
};

enum class EncoderID : oc::type::EncoderID {
    MACRO_1 = 301,
    MACRO_2 = 302,
    MACRO_3 = 303,
    MACRO_4 = 304,
    MACRO_5 = 305,
    MACRO_6 = 306,
    MACRO_7 = 307,
    MACRO_8 = 308,

    NAV = 400,
    OPT = 410,
};

inline constexpr std::size_t MACRO_COUNT = 8;

static_assert(
    static_cast<oc::type::ButtonID>(ButtonID::NAV) < oc::MAX_BUTTONS,
    "OC_MAX_BUTTONS must cover every MIDI Studio device ButtonID");

}  // namespace ms::device_support::v1

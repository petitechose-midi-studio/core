#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <ms/device_support/v1/InputIds.hpp>

#include <oc/type/Ids.hpp>

namespace ms::device_support::v1::control {

/** Physical macro controls in their stable left-to-right slot order. */
inline constexpr std::array<EncoderID, MACRO_COUNT> MACRO_ENCODERS{
    EncoderID::MACRO_1,
    EncoderID::MACRO_2,
    EncoderID::MACRO_3,
    EncoderID::MACRO_4,
    EncoderID::MACRO_5,
    EncoderID::MACRO_6,
    EncoderID::MACRO_7,
    EncoderID::MACRO_8,
};

inline constexpr std::array<ButtonID, MACRO_COUNT> MACRO_BUTTONS{
    ButtonID::MACRO_1,
    ButtonID::MACRO_2,
    ButtonID::MACRO_3,
    ButtonID::MACRO_4,
    ButtonID::MACRO_5,
    ButtonID::MACRO_6,
    ButtonID::MACRO_7,
    ButtonID::MACRO_8,
};

static_assert(MACRO_COUNT <= 0xffU);

inline bool macroEncoderIndex(oc::type::EncoderID id, std::uint8_t& outIndex) {
    for (std::size_t i = 0; i < MACRO_ENCODERS.size(); ++i) {
        if (static_cast<oc::type::EncoderID>(MACRO_ENCODERS[i]) == id) {
            outIndex = static_cast<std::uint8_t>(i);
            return true;
        }
    }
    return false;
}

inline bool macroButtonIndex(oc::type::ButtonID id, std::uint8_t& outIndex) {
    for (std::size_t i = 0; i < MACRO_BUTTONS.size(); ++i) {
        if (static_cast<oc::type::ButtonID>(MACRO_BUTTONS[i]) == id) {
            outIndex = static_cast<std::uint8_t>(i);
            return true;
        }
    }
    return false;
}

}  // namespace ms::device_support::v1::control

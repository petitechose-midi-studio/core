#pragma once

#include <array>

#include <oc/common/ButtonDef.hpp>
#include <oc/common/EncoderDef.hpp>
#include <oc/teensy/GenericMux.hpp>

#include "HardwareDisplay.hpp"
#include "InputIDs.hpp"

namespace Hardware {

// ═══════════════════════════════════════════════════════════════════════════
// Multiplexer (CD74HC4067 - 16 channels)
// ═══════════════════════════════════════════════════════════════════════════

namespace Mux {
constexpr oc::teensy::CD74HC4067::Config CONFIG = {
    .selectPins = {3, 2, 5, 6},  // S0, S1, S2, S3
    .signalPin = 4,
    .settleTimeUs = 20,
    .signalPullup = true
};
}

// ═══════════════════════════════════════════════════════════════════════════
// Encoders (10 total - all MCU direct pins)
// ═══════════════════════════════════════════════════════════════════════════

namespace Encoder {
using namespace oc::common;
using EncoderID = Config::EncoderID;

// Shared parameters for macro encoders
constexpr uint16_t PPR = 24;
constexpr uint16_t RANGE = 270;
constexpr uint8_t TICKS = 1;
constexpr bool INVERT = true;

constexpr std::array ENCODERS = {
    //         id                  pinA pinB  ppr   range  ticks  invert
    EncoderDef(EncoderID::MACRO_1, 22, 23, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_2, 18, 19, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_3, 40, 41, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_4, 36, 37, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_5, 20, 21, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_6, 16, 17, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_7, 14, 15, PPR, RANGE, TICKS, INVERT),
    EncoderDef(EncoderID::MACRO_8, 38, 39, PPR, RANGE, TICKS, INVERT),
    // NAV: 4 ticks per detent for coarser control
    EncoderDef(EncoderID::NAV, 31, 30, 24, 270, 4, !INVERT),
    // OPT: High resolution encoder (600 PPR)
    EncoderDef(EncoderID::OPT, 34, 33, 600, 270, 1, INVERT),
};
}

// ═══════════════════════════════════════════════════════════════════════════
// Buttons (14 total - 1 MCU direct, 13 via MUX)
// ═══════════════════════════════════════════════════════════════════════════

namespace Button {
using namespace oc::common;
using ButtonID = Config::ButtonID;
using Source = oc::hal::GpioPin::Source;

constexpr std::array BUTTONS = {

    // Navigation buttons (left side) - MUX
    //        id                    pin  source        activeLow
    ButtonDef(ButtonID::LEFT_TOP,    {9,  Source::MUX}, true),
    ButtonDef(ButtonID::LEFT_CENTER, {10, Source::MUX}, true),
    ButtonDef(ButtonID::LEFT_BOTTOM, {11, Source::MUX}, true),

    // Navigation buttons (bottom) - MUX
    //        id                      pin  source        activeLow
    ButtonDef(ButtonID::BOTTOM_LEFT,   {14, Source::MUX}, true),
    ButtonDef(ButtonID::BOTTOM_CENTER, {13, Source::MUX}, true),
    ButtonDef(ButtonID::BOTTOM_RIGHT,  {12, Source::MUX}, true),

    // NAV encoder button - MCU direct
    //        id            pin  source        activeLow
    ButtonDef(ButtonID::NAV, {32, Source::MCU}, true),

    // Macro encoder buttons - MUX
    //        id                pin source        activeLow
    ButtonDef(ButtonID::MACRO_1, {7, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_2, {4, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_3, {2, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_4, {0, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_5, {6, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_6, {5, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_7, {3, Source::MUX}, true),
    ButtonDef(ButtonID::MACRO_8, {1, Source::MUX}, true),
};

}

}  // namespace Hardware

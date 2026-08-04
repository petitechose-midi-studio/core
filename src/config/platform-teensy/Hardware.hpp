#pragma once

/**
 * @file Hardware.hpp
 * @brief Hardware pin and peripheral configuration
 */

#include <array>

#include <oc/hal/common/embedded/ButtonDef.hpp>
#include <oc/hal/common/embedded/EncoderDef.hpp>
#include <oc/hal/common/embedded/GpioPin.hpp>
#include <oc/hal/teensy/GenericMux.hpp>

#include "HardwareDisplay.hpp"
#include <config/InputIDs.hpp>
#include <config/Timing.hpp>

namespace Hardware {

// ═══════════════════════════════════════════════════════════════════════════
// Multiplexer (CD74HC4067 - 16 channels)
// ═══════════════════════════════════════════════════════════════════════════

namespace Mux {
constexpr uint8_t BUTTON_READS_PER_APP_TICK = 1;

constexpr oc::hal::teensy::CD74HC4067::Config CONFIG = {
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
using namespace oc::hal::common::embedded;
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
// Buttons (15 total - 1 MCU direct, 14 via MUX)
// ═══════════════════════════════════════════════════════════════════════════

namespace Button {
using namespace oc::hal::common::embedded;
using ButtonID = Config::ButtonID;
using Source = oc::hal::common::embedded::GpioPin::Source;

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

constexpr uint8_t MUX_BUTTON_COUNT = [] {
    uint8_t count = 0;
    for (const auto& button : BUTTONS) {
        if (button.pin.source == Source::MUX) ++count;
    }
    return count;
}();

static_assert(Mux::BUTTON_READS_PER_APP_TICK > 0);
constexpr uint32_t MUX_SCAN_TICKS =
    (MUX_BUTTON_COUNT + Mux::BUTTON_READS_PER_APP_TICK - 1U) /
    Mux::BUTTON_READS_PER_APP_TICK;
constexpr uint32_t MUX_SCAN_PERIOD_US =
    (MUX_SCAN_TICKS * 1'000'000U + Config::Timing::INPUT_APP_ADMISSION_HZ - 1U) /
    Config::Timing::INPUT_APP_ADMISSION_HZ;
static_assert(
    MUX_SCAN_PERIOD_US <= Config::Timing::DEBOUNCE_MS * 1'000U,
    "MUX button scan period must fit inside the product debounce window"
);

}

}  // namespace Hardware

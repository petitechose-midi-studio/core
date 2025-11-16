#pragma once

#include <cstdint>

enum class ButtonID : uint16_t;
enum class EncoderID : uint16_t;
using MidiChannelValue = uint8_t;
using MidiCCValue = uint8_t;
using MidiNoteValue = uint8_t;

enum class ButtonBindingType : uint8_t {
    PRESS,
    RELEASE,
    LONG_PRESS,
    DOUBLE_TAP,
    COMBO
};

enum class EncoderBindingType : uint8_t {
    TURN,
    TURN_WHILE_PRESSED
};

enum class PinMode { PULLUP, PULLDOWN, RAW };

struct GpioPin {
    enum class Source { MCU, MUX };

    Source source = Source::MCU;
    uint8_t pin = 0;
    PinMode mode = PinMode::PULLUP;

    constexpr GpioPin() = default;

    constexpr GpioPin(uint8_t pinNum, PinMode pinMode = PinMode::PULLUP)
        : source(Source::MCU), pin(pinNum), mode(pinMode) {}

    constexpr GpioPin(Source src, uint8_t pinNum, PinMode pinMode = PinMode::PULLUP)
        : source(src), pin(pinNum), mode(pinMode) {}

    constexpr bool isMultiplexed() const {
        return source == Source::MUX;
    }

    constexpr uint8_t getMuxChannel() const {
        return isMultiplexed() ? pin : 0xFF;
    }

    constexpr bool isValid() const {
        if (source == Source::MUX) {
            return pin <= 15;
        }
        return pin <= 99;
    }
};

inline constexpr GpioPin mcuPin(uint8_t pinNum, PinMode mode = PinMode::PULLUP) {
    return GpioPin(GpioPin::Source::MCU, pinNum, mode);
}

inline constexpr GpioPin muxPin(uint8_t channel, PinMode mode = PinMode::PULLUP) {
    return GpioPin(GpioPin::Source::MUX, channel, mode);
}

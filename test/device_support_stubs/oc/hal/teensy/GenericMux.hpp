#pragma once

#include <array>
#include <cstdint>

namespace oc::hal::teensy {

struct CD74HC4067 {
    struct Config {
        std::array<std::uint8_t, 4> selectPins{};
        std::uint8_t signalPin = 0;
        std::uint16_t settleTimeUs = 20;
        bool signalPullup = true;
    };
};

}  // namespace oc::hal::teensy

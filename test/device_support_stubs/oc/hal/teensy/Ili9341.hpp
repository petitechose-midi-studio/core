#pragma once

#include <cstddef>
#include <cstdint>

namespace oc::hal::teensy {

struct Ili9341Config {
    std::uint16_t width = 320;
    std::uint16_t height = 240;
    std::uint16_t csPin = 28;
    std::uint16_t dcPin = 0;
    std::uint16_t rstPin = 29;
    std::uint16_t mosiPin = 26;
    std::uint16_t sckPin = 27;
    std::uint16_t misoPin = 1;
    std::uint32_t spiSpeed = 40'000'000;
    std::uint16_t rotation = 3;
    bool invertDisplay = true;
    std::uint16_t vsyncSpacing = 1;
    std::uint16_t diffGap = 6;
    std::uint16_t irqPriority = 128;
    float lateStartRatio = 0.3f;
    std::uint32_t refreshRate = 60;

    constexpr std::size_t framebufferSize() const {
        return static_cast<std::size_t>(width) * height;
    }
};

}  // namespace oc::hal::teensy

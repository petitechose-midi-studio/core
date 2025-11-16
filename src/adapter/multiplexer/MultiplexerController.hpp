#pragma once

#include <CD74HC4067.h>

#include "config/System.hpp"

class Multiplexer {
public:
    Multiplexer();
    ~Multiplexer() = default;

    Multiplexer(const Multiplexer&) = delete;
    Multiplexer& operator=(const Multiplexer&) = delete;

    bool readDigitalFromChannel(uint8_t channel);

private:
    void selectChannel(uint8_t channel);
    bool readDigital();

    CD74HC4067 mux_;
    uint8_t currentChannel_ = 0;
    uint32_t lastSwitchTimestamp_ = 0;
    bool channelReady_ = true;
};
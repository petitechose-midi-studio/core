#pragma once

#include <cstddef>
#include <cstdint>

namespace Plugins {

class MidiOut {
public:
    virtual void sendSysEx(const uint8_t* data, size_t length) = 0;

    virtual void sendControlChange(uint8_t channel, uint8_t cc, uint8_t value) = 0;

    virtual void flush() = 0;
};

}  // namespace Plugins
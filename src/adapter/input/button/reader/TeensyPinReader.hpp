#pragma once

#include "IPinReader.hpp"

#include <cstdint>

#include "core/Type.hpp"

/**
 * Pin reader with software debouncing for Teensy.
 * Uses simple time-based debouncing without external libraries.
 */
class TeensyPinReader : public IPinReader {
public:
    explicit TeensyPinReader(uint8_t pin, PinMode mode = PinMode::PULLUP);

    void initialize() override;
    bool read() override;
    void update() override;
private:
    uint8_t pin_;
    PinMode mode_;
    bool initialized_ = false;

    // Debounce state
    bool debounced_state_ = false;
    bool last_raw_state_ = false;
    uint32_t last_change_time_ = 0;
};

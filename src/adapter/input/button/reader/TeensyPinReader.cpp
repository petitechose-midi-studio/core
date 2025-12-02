#include "TeensyPinReader.hpp"

#include <Arduino.h>

#include "config/System.hpp"

TeensyPinReader::TeensyPinReader(uint8_t pin, PinMode mode) : pin_(pin), mode_(mode) {}

void TeensyPinReader::initialize() {
    if (initialized_) return;

    int pinModeValue = (mode_ == PinMode::PULLUP)     ? INPUT_PULLUP
                       : (mode_ == PinMode::PULLDOWN) ? INPUT_PULLDOWN
                                                      : INPUT;
    pinMode(pin_, pinModeValue);

    // Initialize state
    debounced_state_ = digitalReadFast(pin_);
    last_raw_state_ = debounced_state_;
    last_change_time_ = millis();
    initialized_ = true;
}

bool TeensyPinReader::read() {
    if (!initialized_) { initialize(); }
    return debounced_state_;
}

void TeensyPinReader::update() {
    if (!initialized_) return;

    bool raw_state = digitalReadFast(pin_);

    if (raw_state != last_raw_state_) {
        // State changed, reset timer
        last_change_time_ = millis();
        last_raw_state_ = raw_state;
    } else if (raw_state != debounced_state_) {
        // State is stable and different from debounced
        if ((millis() - last_change_time_) >= System::Hardware::PIN_DEBOUNCE_MS) {
            debounced_state_ = raw_state;
        }
    }
}

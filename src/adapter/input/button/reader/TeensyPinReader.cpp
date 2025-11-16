#include "TeensyPinReader.hpp"

TeensyPinReader::TeensyPinReader(uint8_t pin, PinMode mode)
    : pin_(pin), mode_(mode), initialized_(false), bounce_() {}

void TeensyPinReader::initialize() {
    if (initialized_) {
        return;
    }

    int pinModeValue = (mode_ == PinMode::PULLUP)     ? INPUT_PULLUP
                       : (mode_ == PinMode::PULLDOWN) ? INPUT_PULLDOWN
                                                      : INPUT;

    bounce_.attach(pin_, pinModeValue);
    bounce_.interval(5);
    initialized_ = true;
}

bool TeensyPinReader::read() {
    if (!initialized_) {
        initialize();
    }

    return bounce_.read();
}

void TeensyPinReader::update() {
    if (!initialized_) {
        return;
    }

    bounce_.update();
}
#pragma once

#include <Bounce2.h>

#include "IPinReader.hpp"
#include "core/Type.hpp"

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
    Bounce bounce_;
};
#pragma once

#include "IPinReader.hpp"

#include <cstdint>

class Multiplexer;

class TeensyMultiplexerPinReader : public IPinReader {
public:
    explicit TeensyMultiplexerPinReader(uint8_t channel, Multiplexer& mux);

    void initialize() override;
    bool read() override;
private:
    uint8_t channel_;
    Multiplexer& mux_;
    bool initialized_ = false;
};
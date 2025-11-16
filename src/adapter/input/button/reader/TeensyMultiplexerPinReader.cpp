#include "TeensyMultiplexerPinReader.hpp"

#include "../../../multiplexer/MultiplexerController.hpp"

TeensyMultiplexerPinReader::TeensyMultiplexerPinReader(uint8_t channel, Multiplexer& mux)
    : channel_(channel), mux_(mux), initialized_(false) {}

void TeensyMultiplexerPinReader::initialize() {
    if (initialized_) {
        return;
    }

    initialized_ = true;
}

bool TeensyMultiplexerPinReader::read() {
    if (!initialized_) {
        initialize();
    }

    return mux_.readDigitalFromChannel(channel_);
}

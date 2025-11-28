#include "MultiplexerController.hpp"

#include <Arduino.h>

#include "log/Macros.hpp"

void Multiplexer::init() {
    if (mux_.has_value()) return;  // Already initialized

    // Create the CD74HC4067 instance - this calls pinMode() for s0-s3
    mux_.emplace(
        System::Hardware::MUX_S0_PIN,
        System::Hardware::MUX_S1_PIN,
        System::Hardware::MUX_S2_PIN,
        System::Hardware::MUX_S3_PIN
    );

    // Configure signal pin
    pinMode(System::Hardware::MUX_SIGNAL_PIN, INPUT_PULLUP);
    selectChannel(0);

    LOGLN("[Mux] Init OK");
}

void Multiplexer::selectChannel(uint8_t channel) {
    if (!mux_.has_value() || channel >= System::Hardware::MUX_MAX_CHANNELS) {
        return;
    }

    if (channel != currentChannel_) {
        mux_->channel(channel);
        currentChannel_ = channel;

        lastSwitchTimestamp_ = micros();
        channelReady_ = false;
    }
}

bool Multiplexer::readDigital() {
    if (!channelReady_) {
        uint32_t elapsed = micros() - lastSwitchTimestamp_;
        if (elapsed < System::Hardware::MUX_DEBOUNCE_US) {
            delayMicroseconds(System::Hardware::MUX_DEBOUNCE_US - elapsed);
        }
        channelReady_ = true;
    }

    return digitalRead(System::Hardware::MUX_SIGNAL_PIN);
}

bool Multiplexer::readDigitalFromChannel(uint8_t channel) {
    selectChannel(channel);
    return readDigital();
}
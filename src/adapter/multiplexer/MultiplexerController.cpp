#include "MultiplexerController.hpp"

#include <Arduino.h>

#include "log/Macros.hpp"

namespace {
    constexpr uint16_t DELAY_PIN_SETTLE_US = 100;
}

void Multiplexer::init() {
    if (mux_.has_value()) return;

    // Create mux instance + configure pins + wait
    LOG("[Mux] Pins +"); LOG(DELAY_PIN_SETTLE_US); LOGLN("us");
    mux_.emplace(
        System::Hardware::MUX_S0_PIN,
        System::Hardware::MUX_S1_PIN,
        System::Hardware::MUX_S2_PIN,
        System::Hardware::MUX_S3_PIN
    );
    pinMode(System::Hardware::MUX_SIGNAL_PIN, INPUT_PULLUP);
    delayMicroseconds(DELAY_PIN_SETTLE_US);

    selectChannel(0);
    LOGLN("[Mux] OK");
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
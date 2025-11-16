#include "MultiplexerController.hpp"

#include "log/Macros.hpp"

Multiplexer::Multiplexer()
    : mux_(System::Hardware::MUX_S0_PIN, System::Hardware::MUX_S1_PIN, System::Hardware::MUX_S2_PIN,
           System::Hardware::MUX_S3_PIN) {
    pinMode(System::Hardware::MUX_SIGNAL_PIN, INPUT_PULLUP);
    selectChannel(0);
}

void Multiplexer::selectChannel(uint8_t channel) {
    if (channel >= System::Hardware::MUX_MAX_CHANNELS) {
        return;
    }

    if (channel != currentChannel_) {
        mux_.channel(channel);
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
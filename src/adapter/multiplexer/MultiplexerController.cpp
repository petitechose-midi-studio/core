#include "MultiplexerController.hpp"

#include <Arduino.h>

#include "config/System.hpp"
#include "log/Macros.hpp"

namespace {
constexpr uint16_t DELAY_PIN_SETTLE_US = 100;

// Select pins for CD74HC4067
constexpr uint8_t MUX_PINS[4] = {System::Hardware::MUX_S0_PIN, System::Hardware::MUX_S1_PIN,
                                 System::Hardware::MUX_S2_PIN, System::Hardware::MUX_S3_PIN};

inline void setMuxChannel(uint8_t channel) {
    // Set 4 select pins using bit operations (channel 0-15 = 4 bits)
    digitalWriteFast(MUX_PINS[0], channel & 0x01);
    digitalWriteFast(MUX_PINS[1], (channel >> 1) & 0x01);
    digitalWriteFast(MUX_PINS[2], (channel >> 2) & 0x01);
    digitalWriteFast(MUX_PINS[3], (channel >> 3) & 0x01);
}
}  // namespace

void Multiplexer::init() {
    if (initialized_) return;

    LOG("[Mux] Pins +");
    LOG(DELAY_PIN_SETTLE_US);
    LOGLN("us");

    // Configure select pins as outputs
    for (uint8_t pin : MUX_PINS) { pinMode(pin, OUTPUT); }
    pinMode(System::Hardware::MUX_SIGNAL_PIN, INPUT_PULLUP);
    delayMicroseconds(DELAY_PIN_SETTLE_US);

    selectChannel(0);
    initialized_ = true;
    LOGLN("[Mux] OK");
}

void Multiplexer::selectChannel(uint8_t channel) {
    if (!initialized_ || channel >= System::Hardware::MUX_MAX_CHANNELS) { return; }

    if (channel != current_channel_) {
        setMuxChannel(channel);
        current_channel_ = channel;

        last_switch_timestamp_ = micros();
        channel_ready_ = false;
    }
}

bool Multiplexer::readDigital() {
    if (!channel_ready_) {
        uint32_t elapsed = micros() - last_switch_timestamp_;
        if (elapsed < System::Hardware::MUX_DEBOUNCE_US) {
            delayMicroseconds(System::Hardware::MUX_DEBOUNCE_US - elapsed);
        }
        channel_ready_ = true;
    }

    return digitalReadFast(System::Hardware::MUX_SIGNAL_PIN);
}

bool Multiplexer::readDigitalFromChannel(uint8_t channel) {
    selectChannel(channel);
    return readDigital();
}

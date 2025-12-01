#pragma once

#include <cstdint>

#include "config/System.hpp"

/**
 * Multiplexer controller for CD74HC4067 (16-channel analog mux).
 *
 * IMPORTANT: Hardware init (pinMode, digitalWrite) is deferred to init()
 * to avoid calling Arduino functions before the framework is ready.
 * This is critical when the object is instantiated as a global variable.
 */
class Multiplexer {
public:
    Multiplexer() = default;
    ~Multiplexer() = default;

    Multiplexer(const Multiplexer&) = delete;
    Multiplexer& operator=(const Multiplexer&) = delete;

    /**
     * Initialize hardware. Must be called after Arduino setup().
     * Safe to call multiple times - subsequent calls are no-ops.
     */
    void init();
    bool isInitialized() const { return initialized_; }

    bool readDigitalFromChannel(uint8_t channel);

private:
    void selectChannel(uint8_t channel);
    bool readDigital();

    bool initialized_ = false;
    uint8_t current_channel_ = 0;
    uint32_t last_switch_timestamp_ = 0;
    bool channel_ready_ = true;
};

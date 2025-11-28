#pragma once

#include <CD74HC4067.h>
#include <etl/optional.h>

#include "config/System.hpp"

/**
 * Multiplexer controller with lazy initialization.
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
    bool isInitialized() const { return mux_.has_value(); }

    bool readDigitalFromChannel(uint8_t channel);

private:
    void selectChannel(uint8_t channel);
    bool readDigital();

    etl::optional<CD74HC4067> mux_;
    uint8_t currentChannel_ = 0;
    uint32_t lastSwitchTimestamp_ = 0;
    bool channelReady_ = true;
};
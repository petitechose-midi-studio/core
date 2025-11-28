#pragma once

#include <Arduino.h>
#include <EncoderTool.h>

#include <memory>

#include "core/event/IEventBus.hpp"
#include "core/struct/Encoder.hpp"

/**
 * Encoder with lazy hardware initialization.
 *
 * IMPORTANT: Hardware init (attachInterrupt, pinMode) is deferred to init()
 * to avoid calling Arduino functions before the framework is ready.
 * This is critical when the object is instantiated as a global variable.
 */
class Encoder {
public:
    explicit Encoder(const Hardware::Encoder& setup, IEventBus& eventBus);
    ~Encoder();

    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    Encoder(Encoder&&) = delete;
    Encoder& operator=(Encoder&&) = delete;

    /**
     * Initialize hardware (interrupts, pins). Must be called after Arduino setup().
     * Safe to call multiple times - subsequent calls are no-ops.
     */
    void init();
    bool isInitialized() const { return initialized_; }

    void flushEvents();
    void resetPosition(float normalizedValue);

    void setDiscreteSteps(uint8_t steps);
    void setContinuous();

    void setMode(Hardware::EncoderMode mode);
    void setBounds(float min, float max);
    void setDelta(float delta);

    EncoderID getId() const {
        return id_;
    }

    Hardware::EncoderMode getMode() const {
        return mode_;
    }

private:
    EncoderID id_;
    EncoderTool::Encoder encoder_;
    Hardware::EncoderMode mode_;
    uint16_t ppr_;
    uint8_t stepsPerDetent_;

    // Pins stored for deferred init
    uint8_t pinA_;
    uint8_t pinB_;
    bool initialized_ = false;

    int32_t virtualRange_;
    int32_t virtualPosition_;
    float lastNormalizedValue_;

    int32_t accumulatedDelta_;
    float relativePosition_;

    IEventBus& eventBus_;

    volatile bool hasPendingEvent_;
    float pendingValue_;

    uint8_t discreteSteps_;
    float lastQuantizedValue_;

    float minBound_;
    float maxBound_;
    bool hasBounds_;
    float deltaPerDetent_;

    void processEncoderChange(int32_t delta);
    void handleRelativeMode(int32_t delta);
    void handleAbsoluteMode(int32_t delta);

    int32_t calculateDefaultVirtualRange() const;
    bool applyQuantization(float normalizedValue, float& outValue);
    void emitPendingEvent(float value);
};

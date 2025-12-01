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
    uint8_t steps_per_detent_;

    // Pins stored for deferred init
    uint8_t pin_a_;
    uint8_t pin_b_;
    bool initialized_ = false;

    int32_t virtual_range_;
    int32_t virtual_position_;
    float last_normalized_value_;

    int32_t accumulated_delta_;
    float relative_position_;

    IEventBus& event_bus_;

    volatile bool has_pending_event_;
    float pending_value_;

    uint8_t discrete_steps_;
    float last_quantized_value_;

    float min_bound_;
    float max_bound_;
    bool has_bounds_;
    float delta_per_detent_;

    void processEncoderChange(int32_t delta);
    void handleRelativeMode(int32_t delta);
    void handleAbsoluteMode(int32_t delta);

    int32_t calculateDefaultVirtualRange() const;
    bool applyQuantization(float normalizedValue, float& outValue);
    void emitPendingEvent(float value);
};

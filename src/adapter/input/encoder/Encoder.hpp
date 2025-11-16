#pragma once

#include <Arduino.h>
#include <EncoderTool.h>

#include <memory>

#include "core/event/IEventBus.hpp"
#include "core/struct/Encoder.hpp"

class Encoder {
public:
    explicit Encoder(const Hardware::Encoder& setup, IEventBus& eventBus);
    ~Encoder();

    Encoder(const Encoder&) = delete;
    Encoder& operator=(const Encoder&) = delete;
    Encoder(Encoder&&) = delete;
    Encoder& operator=(Encoder&&) = delete;

    void flushEvents();
    void resetPosition(float normalizedValue);

    void setDiscreteSteps(uint8_t steps);
    void setContinuous();

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

    void processEncoderChange(int32_t delta);
    void handleRelativeMode(int32_t delta);
    void handleAbsoluteMode(int32_t delta);

    int32_t calculateDefaultVirtualRange() const;
    bool applyQuantization(float normalizedValue, float& outValue);
    void emitPendingEvent(float value);
};

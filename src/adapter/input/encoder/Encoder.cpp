#include "Encoder.hpp"

#include <Arduino.h>

#include "core/event/Events.hpp"

namespace {
constexpr uint8_t TICK_COUNT_METHOD = 4; // Full Quadrature Mode
constexpr uint16_t FULL_RANGE_ANGLE = 270;
constexpr float DISCRETE_VALUES_SENSITIVITY = 0.5;
}

Encoder::Encoder(const Hardware::Encoder& setup, IEventBus& eventBus)
    : id_(setup.id),
      encoder_(),
      mode_(setup.mode),
      ppr_(setup.ppr),
      stepsPerDetent_(setup.stepsPerDetent),
      virtualRange_(0),
      virtualPosition_(0),
      lastNormalizedValue_(0.5f),
      accumulatedDelta_(0),
      relativePosition_(0.0f),
      eventBus_(eventBus),
      hasPendingEvent_(false),
      pendingValue_(0.0f),
      discreteSteps_(0),
      lastQuantizedValue_(-1.0f) {
    virtualRange_ = calculateDefaultVirtualRange();
    virtualPosition_ = virtualRange_ / 2;

    encoder_.begin(setup.pinA.pin, setup.pinB.pin, EncoderTool::CountMode::full);
    encoder_.attachCallback([this](int, int delta) { this->processEncoderChange(delta); });
}

Encoder::~Encoder() = default;

void Encoder::flushEvents() {
    if (!hasPendingEvent_) return;

    hasPendingEvent_ = false;
    eventBus_.emit(EncoderChangedEvent(id_, pendingValue_));
}

void Encoder::resetPosition(float normalizedValue) {
    if (mode_ == Hardware::EncoderMode::Relative) {
        relativePosition_ = normalizedValue;
        accumulatedDelta_ = 0;
        hasPendingEvent_ = false;
        return;
    }

    normalizedValue = constrain(normalizedValue, 0.0f, 1.0f);
    virtualPosition_ = static_cast<int32_t>(normalizedValue * (virtualRange_ - 1));
    lastNormalizedValue_ = normalizedValue;
    hasPendingEvent_ = false;
}

void Encoder::setDiscreteSteps(uint8_t steps) {
    if (mode_ != Hardware::EncoderMode::Absolute) return;

    discreteSteps_ = steps;
    lastQuantizedValue_ = -1.0f;

    int32_t defaultRange = calculateDefaultVirtualRange();
    int32_t minRangeForSteps = steps * (1.0 / DISCRETE_VALUES_SENSITIVITY);

    virtualRange_ = (steps > 0 && minRangeForSteps > defaultRange)
        ? minRangeForSteps
        : defaultRange;

    virtualPosition_ = static_cast<int32_t>(lastNormalizedValue_ * (virtualRange_ - 1));
}

void Encoder::setContinuous() {
    setDiscreteSteps(0);
}

void Encoder::processEncoderChange(int32_t delta) {
    if (delta == 0) return;

    if (mode_ == Hardware::EncoderMode::Relative) {
        handleRelativeMode(delta);
    } else {
        handleAbsoluteMode(delta);
    }
}

void Encoder::handleRelativeMode(int32_t delta) {
    accumulatedDelta_ += delta;

    bool shouldEmit = abs(accumulatedDelta_) >= stepsPerDetent_;
    if (!shouldEmit) return;

    float step = (accumulatedDelta_ > 0) ? 1.0f : -1.0f;
    relativePosition_ += step;
    accumulatedDelta_ = 0;

    emitPendingEvent(relativePosition_);
}

void Encoder::handleAbsoluteMode(int32_t delta) {
    int32_t movement = (delta > 0) ? -1 : 1;
    virtualPosition_ = constrain(virtualPosition_ + movement, 0, virtualRange_ - 1);

    float normalizedValue = virtualPosition_ / static_cast<float>(virtualRange_ - 1);

    if (normalizedValue == lastNormalizedValue_) return;
    lastNormalizedValue_ = normalizedValue;

    float valueToEmit = normalizedValue;
    if (applyQuantization(normalizedValue, valueToEmit)) {
        emitPendingEvent(valueToEmit);
    }
}

bool Encoder::applyQuantization(float normalizedValue, float& outValue) {
    if (discreteSteps_ == 0) {
        outValue = normalizedValue;
        return true;
    }

    float quantized = round(normalizedValue * (discreteSteps_ - 1)) / (discreteSteps_ - 1);

    if (quantized == lastQuantizedValue_) {
        return false;
    }

    lastQuantizedValue_ = quantized;
    outValue = quantized;
    return true;
}

void Encoder::emitPendingEvent(float value) {
    pendingValue_ = value;
    hasPendingEvent_ = true;
}

int32_t Encoder::calculateDefaultVirtualRange() const {
    return (ppr_ * TICK_COUNT_METHOD) * (FULL_RANGE_ANGLE / 360.0f);
}

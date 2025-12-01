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
      steps_per_detent_(setup.stepsPerDetent),
      pin_a_(setup.pinA.pin),
      pin_b_(setup.pinB.pin),
      initialized_(false),
      virtual_range_(0),
      virtual_position_(0),
      last_normalized_value_(0.5f),
      accumulated_delta_(0),
      relative_position_(0.0f),
      event_bus_(eventBus),
      has_pending_event_(false),
      pending_value_(0.0f),
      discrete_steps_(0),
      last_quantized_value_(-1.0f),
      min_bound_(0.0f),
      max_bound_(1.0f),
      has_bounds_(false),
      delta_per_detent_(1.0f) {
    // NOTE: Do NOT call encoder_.begin() here!
    // Hardware init is deferred to init() to support global object instantiation
    virtual_range_ = calculateDefaultVirtualRange();
    virtual_position_ = virtual_range_ / 2;
}

void Encoder::init() {
    if (initialized_) return;

    encoder_.begin(pin_a_, pin_b_, EncoderTool::CountMode::full);
    encoder_.attachCallback([this](int, int delta) { this->processEncoderChange(delta); });

    initialized_ = true;
}

Encoder::~Encoder() = default;

void Encoder::flushEvents() {
    if (!has_pending_event_) return;

    has_pending_event_ = false;
    event_bus_.emit(EncoderChangedEvent(id_, pending_value_));
}

void Encoder::resetPosition(float normalizedValue) {
    if (mode_ == Hardware::EncoderMode::Relative) {
        relative_position_ = normalizedValue;
        accumulated_delta_ = 0;
        has_pending_event_ = false;
        return;
    }

    normalizedValue = constrain(normalizedValue, 0.0f, 1.0f);
    virtual_position_ = static_cast<int32_t>(normalizedValue * (virtual_range_ - 1));
    last_normalized_value_ = normalizedValue;
    has_pending_event_ = false;
}

void Encoder::setDiscreteSteps(uint8_t steps) {
    if (mode_ != Hardware::EncoderMode::Absolute) return;

    discrete_steps_ = steps;
    last_quantized_value_ = -1.0f;

    int32_t defaultRange = calculateDefaultVirtualRange();
    int32_t minRangeForSteps = steps * (1.0 / DISCRETE_VALUES_SENSITIVITY);

    virtual_range_ = (steps > 0 && minRangeForSteps > defaultRange)
        ? minRangeForSteps
        : defaultRange;

    virtual_position_ = static_cast<int32_t>(last_normalized_value_ * (virtual_range_ - 1));
}

void Encoder::setContinuous() {
    setDiscreteSteps(0);
}

void Encoder::setMode(Hardware::EncoderMode mode) {
    mode_ = mode;
    // Reset state when switching modes
    if (mode_ == Hardware::EncoderMode::Relative) {
        accumulated_delta_ = 0;
    } else {
        virtual_position_ = virtual_range_ / 2;
        last_normalized_value_ = 0.5f;
    }
}

void Encoder::setBounds(float min, float max) {
    min_bound_ = min;
    max_bound_ = max;
    has_bounds_ = true;

    // Clamp current position if in Relative mode
    if (mode_ == Hardware::EncoderMode::Relative && has_bounds_) {
        relative_position_ = constrain(relative_position_, min_bound_, max_bound_);
    }
}

void Encoder::setDelta(float delta) {
    delta_per_detent_ = delta;
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
    accumulated_delta_ += delta;

    bool shouldEmit = abs(accumulated_delta_) >= steps_per_detent_;
    if (!shouldEmit) return;

    float step = (accumulated_delta_ > 0) ? delta_per_detent_ : -delta_per_detent_;

    // In Relative mode, emit the delta (step), not cumulative position
    accumulated_delta_ = 0;
    emitPendingEvent(step);
}

void Encoder::handleAbsoluteMode(int32_t delta) {
    int32_t movement = (delta > 0) ? -1 : 1;
    virtual_position_ = constrain(virtual_position_ + movement, 0, virtual_range_ - 1);

    float normalizedValue = virtual_position_ / static_cast<float>(virtual_range_ - 1);

    if (normalizedValue == last_normalized_value_) return;
    last_normalized_value_ = normalizedValue;

    // Map to bounds if configured, otherwise use normalized [0.0-1.0]
    float valueToEmit = has_bounds_
        ? min_bound_ + (normalizedValue * (max_bound_ - min_bound_))
        : normalizedValue;

    if (applyQuantization(valueToEmit, valueToEmit)) {
        emitPendingEvent(valueToEmit);
    }
}

bool Encoder::applyQuantization(float normalizedValue, float& outValue) {
    if (discrete_steps_ == 0) {
        outValue = normalizedValue;
        return true;
    }

    float quantized = round(normalizedValue * (discrete_steps_ - 1)) / (discrete_steps_ - 1);

    if (quantized == last_quantized_value_) {
        return false;
    }

    last_quantized_value_ = quantized;
    outValue = quantized;
    return true;
}

void Encoder::emitPendingEvent(float value) {
    pending_value_ = value;
    has_pending_event_ = true;
}

int32_t Encoder::calculateDefaultVirtualRange() const {
    return (ppr_ * TICK_COUNT_METHOD) * (FULL_RANGE_ANGLE / 360.0f);
}

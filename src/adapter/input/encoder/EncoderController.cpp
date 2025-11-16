#include "EncoderController.hpp"

EncoderController::EncoderController(
    const etl::vector<Hardware::Encoder, System::Hardware::ENCODERS_COUNT>& encoderSetups,
    IEventBus& eventBus) {
    for (auto& setup : encoderSetups) {
        size_t index = encoders_.size();
        encoders_.emplace_back(setup, eventBus);  // Construct in-place on stack
        idToIndex_[setup.id] = index;             // Map InputId -> array index
    }
}

void EncoderController::flushAllEvents() {
    for (auto& encoder : encoders_) {
        encoder.flushEvents();
    }
}

void EncoderController::resetEncoderPosition(EncoderID encoderId, float normalizedValue) {
    Encoder* encoder = getEncoder(encoderId);
    if (encoder) {
        encoder->resetPosition(normalizedValue);
    }
}

void EncoderController::setDiscreteSteps(EncoderID encoderId, uint8_t steps) {
    Encoder* encoder = getEncoder(encoderId);
    if (encoder) {
        encoder->setDiscreteSteps(steps);
    }
}

void EncoderController::setContinuous(EncoderID encoderId) {
    Encoder* encoder = getEncoder(encoderId);
    if (encoder) {
        encoder->setContinuous();
    }
}

Encoder* EncoderController::getEncoder(EncoderID id) {
    auto it = idToIndex_.find(id);
    return (it != idToIndex_.end()) ? &encoders_[it->second] : nullptr;
}

const Encoder* EncoderController::getEncoder(EncoderID id) const {
    auto it = idToIndex_.find(id);
    return (it != idToIndex_.end()) ? &encoders_[it->second] : nullptr;
}

#include "EncoderController.hpp"

EncoderController::EncoderController(const std::vector<Hardware::Encoder>& encoderSetups,
                                     IEventBus& eventBus) {
    encoders_.reserve(encoderSetups.size());
    for (const auto& setup : encoderSetups) {
        size_t index = encoders_.size();
        encoders_.push_back(std::make_unique<Encoder>(setup, eventBus));
        id_to_index_[setup.id] = index;
    }
}

void EncoderController::init() {
    for (auto& encoder : encoders_) { encoder->init(); }
}

void EncoderController::flushAllEvents() {
    for (auto& encoder : encoders_) { encoder->flushEvents(); }
}

void EncoderController::resetEncoderPosition(EncoderID encoderId, float normalizedValue) {
    Encoder* encoder = getEncoder(encoderId);
    if (encoder) { encoder->resetPosition(normalizedValue); }
}

void EncoderController::setDiscreteSteps(EncoderID encoderId, uint16_t steps) {
    Encoder* encoder = getEncoder(encoderId);
    if (encoder) { encoder->setDiscreteSteps(steps); }
}

void EncoderController::setContinuous(EncoderID encoderId) {
    Encoder* encoder = getEncoder(encoderId);
    if (encoder) { encoder->setContinuous(); }
}

void EncoderController::setMode(EncoderID encoderId, Hardware::EncoderMode mode) {
    Encoder* encoder = getEncoder(encoderId);
    if (encoder) { encoder->setMode(mode); }
}

void EncoderController::setBounds(EncoderID encoderId, float min, float max) {
    Encoder* encoder = getEncoder(encoderId);
    if (encoder) { encoder->setBounds(min, max); }
}

void EncoderController::setDelta(EncoderID encoderId, float delta) {
    Encoder* encoder = getEncoder(encoderId);
    if (encoder) { encoder->setDelta(delta); }
}

Encoder* EncoderController::getEncoder(EncoderID id) {
    auto it = id_to_index_.find(id);
    return (it != id_to_index_.end()) ? encoders_[it->second].get() : nullptr;
}

const Encoder* EncoderController::getEncoder(EncoderID id) const {
    auto it = id_to_index_.find(id);
    return (it != id_to_index_.end()) ? encoders_[it->second].get() : nullptr;
}

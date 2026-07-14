#pragma once

#include <unordered_map>
#include <utility>

#include <oc/interface/IButton.hpp>
#include <oc/interface/IEncoder.hpp>

namespace test_support {

class TestButtonHardware : public oc::interface::IButton {
public:
    oc::type::Result<void> init() override {
        return oc::type::Result<void>::ok();
    }

    void update(uint32_t) override {}

    bool isPressed(oc::type::ButtonID id) const override {
        const auto it = pressed_.find(id);
        return it != pressed_.end() ? it->second : false;
    }

    void setCallback(oc::type::ButtonCallback cb) override {
        callback_ = std::move(cb);
    }

    void setPressed(oc::type::ButtonID id, bool pressed) {
        pressed_[id] = pressed;
    }

private:
    std::unordered_map<oc::type::ButtonID, bool> pressed_;
    oc::type::ButtonCallback callback_{};
};

class TestEncoderHardware : public oc::interface::IEncoder {
public:
    oc::type::Result<void> init() override {
        return oc::type::Result<void>::ok();
    }

    void update() override {}

    float getPosition(oc::type::EncoderID id) const override {
        const auto it = positions_.find(id);
        return it != positions_.end() ? it->second : 0.0f;
    }

    void setPosition(oc::type::EncoderID id, float value) override {
        positions_[id] = value;
    }

    void setMode(oc::type::EncoderID id, oc::interface::EncoderMode mode) override {
        modes_[id] = mode;
    }
    void setBounds(oc::type::EncoderID id, float min, float max) override {
        bounds_[id] = {min, max};
    }
    void setDelta(oc::type::EncoderID id, float delta) override {
        deltas_[id] = delta;
    }
    void setDiscreteSteps(oc::type::EncoderID id, uint8_t steps) override {
        discrete_steps_[id] = steps;
    }
    void setDiscreteTicksPerStep(oc::type::EncoderID id, uint16_t ticks) override {
        discrete_ticks_per_step_[id] = ticks;
    }
    void setNormalizedTurns(oc::type::EncoderID id, float turns) override {
        normalized_turns_[id] = turns;
    }
    void setContinuous(oc::type::EncoderID id) override {
        discrete_steps_[id] = 0;
    }

    void setCallback(oc::type::EncoderCallback cb) override {
        callback_ = std::move(cb);
    }

    uint8_t getDiscreteSteps(oc::type::EncoderID id) const {
        const auto it = discrete_steps_.find(id);
        return it != discrete_steps_.end() ? it->second : 0;
    }

    uint16_t getDiscreteTicksPerStep(oc::type::EncoderID id) const {
        const auto it = discrete_ticks_per_step_.find(id);
        return it != discrete_ticks_per_step_.end() ? it->second : 0;
    }

    float getNormalizedTurns(oc::type::EncoderID id) const {
        const auto it = normalized_turns_.find(id);
        return it != normalized_turns_.end() ? it->second : 0.0f;
    }

    oc::interface::EncoderMode getMode(oc::type::EncoderID id) const {
        const auto it = modes_.find(id);
        return it != modes_.end()
            ? it->second
            : oc::interface::EncoderMode::NORMALIZED;
    }

    float getBoundsMin(oc::type::EncoderID id) const {
        const auto it = bounds_.find(id);
        return it != bounds_.end() ? it->second.first : 0.0f;
    }

    float getBoundsMax(oc::type::EncoderID id) const {
        const auto it = bounds_.find(id);
        return it != bounds_.end() ? it->second.second : 1.0f;
    }

    float getDelta(oc::type::EncoderID id) const {
        const auto it = deltas_.find(id);
        return it != deltas_.end() ? it->second : 0.0f;
    }

private:
    std::unordered_map<oc::type::EncoderID, float> positions_;
    std::unordered_map<oc::type::EncoderID, uint8_t> discrete_steps_;
    std::unordered_map<oc::type::EncoderID, uint16_t> discrete_ticks_per_step_;
    std::unordered_map<oc::type::EncoderID, float> normalized_turns_;
    std::unordered_map<oc::type::EncoderID, oc::interface::EncoderMode> modes_;
    std::unordered_map<oc::type::EncoderID, std::pair<float, float>> bounds_;
    std::unordered_map<oc::type::EncoderID, float> deltas_;
    oc::type::EncoderCallback callback_{};
};

}  // namespace test_support

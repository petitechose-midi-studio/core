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

    void setMode(oc::type::EncoderID, oc::interface::EncoderMode) override {}
    void setBounds(oc::type::EncoderID, float, float) override {}
    void setDelta(oc::type::EncoderID, float) override {}
    void setDiscreteSteps(oc::type::EncoderID, uint8_t) override {}
    void setDiscreteTicksPerStep(oc::type::EncoderID, uint16_t) override {}
    void setNormalizedTurns(oc::type::EncoderID, float) override {}
    void setContinuous(oc::type::EncoderID) override {}

    void setCallback(oc::type::EncoderCallback cb) override {
        callback_ = std::move(cb);
    }

private:
    std::unordered_map<oc::type::EncoderID, float> positions_;
    oc::type::EncoderCallback callback_{};
};

}  // namespace test_support

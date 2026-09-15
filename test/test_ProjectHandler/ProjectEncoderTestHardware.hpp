#pragma once

#include <memory>
#include <oc/core/input/EncoderLogic.hpp>
#include "../support/InputTestHardware.hpp"

/** Optional real tick/quantization path for Project's binding tests. The existing
 * explicit-value tests retain their simple hardware/configuration observations.
 */
class ProjectEncoderTestHardware : public test_support::TestEncoderHardware {
    using Id = oc::type::EncoderID;
    using Logic = oc::core::input::EncoderLogic;
    std::unordered_map<Id, std::unique_ptr<Logic>> logic_;
    Logic* logic(Id id) const {
        const auto it = logic_.find(id);
        return it == logic_.end() ? nullptr : it->second.get();
    }
public:
    void enableTicks() {
        for (auto id : Config::MACRO_ENCODERS) {
            const auto key = static_cast<Id>(id);
            logic_.emplace(key, std::make_unique<Logic>(oc::core::input::EncoderConfig{.id = key}));
        }
    }
    void ticks(Config::EncoderID id, int delta, oc::core::event::EventBus& bus) {
        const auto key = static_cast<Id>(id);
        auto* encoder = logic(key);
        assert(encoder);
        encoder->publishDeltaFromISR(delta);
        if (const auto value = encoder->consumePublishedDeltas()) {
            bus.emit(oc::core::event::EncoderChangedEvent(key, *value));
        }
    }
    float getPosition(Id id) const override {
        return logic(id) ? logic(id)->getLastValue() : TestEncoderHardware::getPosition(id);
    }
    void setPosition(Id id, float value) override {
        TestEncoderHardware::setPosition(id, value);
        if (auto* encoder = logic(id)) encoder->setPosition(value);
    }
    void setMode(Id id, oc::interface::EncoderMode mode) override {
        TestEncoderHardware::setMode(id, mode);
        if (auto* encoder = logic(id)) encoder->setMode(mode);
    }
    void setBounds(Id id, float min, float max) override {
        TestEncoderHardware::setBounds(id, min, max);
        if (auto* encoder = logic(id)) encoder->setBounds(min, max);
    }
    void configureResolution(Id id, uint8_t steps, uint16_t ticks, float turns) override {
        TestEncoderHardware::configureResolution(id, steps, ticks, turns);
        if (auto* encoder = logic(id)) encoder->configureResolution(steps, ticks, turns);
    }
};

#pragma once

#include <memory>
#include <utility>
#include <oc/interface/IButton.hpp>

namespace core::validation::benchmark {

class BenchButtons;
inline BenchButtons* currentBenchButtons = nullptr;

// Preserve real polling/callbacks, while ButtonAPI queries see scripted held
// buttons too. The endpoint updates synthetic state before emitting its event.
class BenchButtons final : public oc::interface::IButton {
public:
    static constexpr uint16_t BUTTON_COUNT = 48U;
    explicit BenchButtons(std::unique_ptr<oc::interface::IButton> physical)
        : physical_(std::move(physical)) { currentBenchButtons = this; }
    ~BenchButtons() override {
        if (currentBenchButtons == this) currentBenchButtons = nullptr;
    }
    oc::type::Result<void> init() override {
        if (!physical_) return oc::type::Result<void>::err({oc::type::ErrorCode::HARDWARE_NOT_FOUND});
        return physical_->init();
    }
    void update(uint32_t nowMs) override { if (physical_) physical_->update(nowMs); }
    bool isPressed(oc::type::ButtonID id) const override {
        return (id < BUTTON_COUNT && (synthetic_ & (uint64_t(1) << id))) ||
               (physical_ && physical_->isPressed(id));
    }
    void setCallback(oc::type::ButtonCallback callback) override {
        if (physical_) physical_->setCallback(std::move(callback));
    }
    bool setSynthetic(uint16_t id, bool down) {
        if (id >= BUTTON_COUNT) return false;
        const uint64_t bit = uint64_t(1) << id;
        if (down) synthetic_ |= bit;
        else synthetic_ &= ~bit;
        return true;
    }
    void clearSynthetic() { synthetic_ = 0; }
    bool allPhysicalReleased() const {
        if (!physical_) return false;
        for (uint16_t id = 0; id < BUTTON_COUNT; ++id)
            if (physical_->isPressed(id)) return false;
        return true;
    }

private:
    std::unique_ptr<oc::interface::IButton> physical_;
    uint64_t synthetic_ = 0;
};

inline bool setSyntheticButton(uint16_t id, bool down) {
    return currentBenchButtons && currentBenchButtons->setSynthetic(id, down);
}
inline void clearSyntheticButtons() {
    if (currentBenchButtons) currentBenchButtons->clearSynthetic();
}
inline bool allPhysicalReleased() {
    return currentBenchButtons && currentBenchButtons->allPhysicalReleased();
}

}  // namespace core::validation::benchmark

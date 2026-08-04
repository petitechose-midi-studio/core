#pragma once

#include <cassert>
#include <cstdint>

#include <oc/time/Time.hpp>

namespace test_support {

/** Deterministic native-test clock; each provider read advances before return. */
class AdvancingMicrosClock final {
public:
    void install(uint32_t initialUs = 0U) {
        active_ = this;
        freezeAt(initialUs);
        oc::time::setMicrosProvider(&AdvancingMicrosClock::provide_);
    }

    void freezeAt(uint32_t nowUs) {
        reset(nowUs, 0U);
    }

    void advanceOnReadFrom(uint32_t nowUs, uint32_t incrementPerReadUs) {
        reset(nowUs, incrementPerReadUs);
    }

    [[nodiscard]] uint32_t currentUs() const {
        return now_us_;
    }

    [[nodiscard]] uint32_t incrementPerReadUs() const {
        return increment_per_read_us_;
    }

    [[nodiscard]] uint32_t readCount() const {
        return read_count_;
    }

private:
    void reset(uint32_t nowUs, uint32_t incrementPerReadUs) {
        now_us_ = nowUs;
        increment_per_read_us_ = incrementPerReadUs;
        read_count_ = 0U;
    }

    static uint32_t provide_() {
        assert(active_ != nullptr);
        active_->now_us_ += active_->increment_per_read_us_;
        ++active_->read_count_;
        return active_->now_us_;
    }

    inline static AdvancingMicrosClock* active_ = nullptr;
    uint32_t now_us_ = 0U;
    uint32_t increment_per_read_us_ = 0U;
    uint32_t read_count_ = 0U;
};

}  // namespace test_support

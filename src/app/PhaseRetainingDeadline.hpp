#pragma once

#include <cstdint>

namespace core::app {

/**
 * One allocation-free periodic deadline with bounded catch-up semantics.
 *
 * PeriodUs and every supported observation gap must be shorter than the
 * uint32_t half-range. A due observation consumes exactly once, retains the
 * original phase after ordinary overshoot, and skips missed periods directly.
 */
template<uint32_t PeriodUs>
class PhaseRetainingDeadline {
    static constexpr uint32_t HALF_RANGE_US = 0x80000000U;

    static_assert(PeriodUs > 0U, "Periodic deadline requires a non-zero period");
    static_assert(
        PeriodUs < HALF_RANGE_US,
        "Periodic deadline period must fit inside the uint32_t half-range"
    );

public:
    constexpr PhaseRetainingDeadline() noexcept = default;

    explicit constexpr PhaseRetainingDeadline(uint32_t phaseUs) noexcept
        : phase_us_(phaseUs) {}

    [[nodiscard]] bool consumeIfDue(uint32_t nowUs) noexcept {
        const uint32_t deadlineUs = phase_us_ + PeriodUs;
        const uint32_t latenessUs = nowUs - deadlineUs;
        if (latenessUs >= HALF_RANGE_US) return false;

        phase_us_ = deadlineUs;
        if (latenessUs >= PeriodUs) {
            phase_us_ += (latenessUs / PeriodUs) * PeriodUs;
        }
        return true;
    }

private:
    uint32_t phase_us_ = 0U;
};

}  // namespace core::app

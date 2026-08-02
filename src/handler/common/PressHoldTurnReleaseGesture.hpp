#pragma once

#include <cstdint>

namespace core::handler {

/**
 * Allocation-free ownership state for a press/hold/turn/release gesture.
 * The release edge is the sole decision point; callers keep domain-specific
 * preview and commit policy outside this tiny shared primitive.
 */
class PressHoldTurnReleaseGesture {
public:
    enum class Release : uint8_t {
        NONE = 0,
        TAP,
        HOLD,
        TURN,
    };

    void press() {
        state_ = PRESSED;
    }

    void hold() {
        if (active()) state_ |= HELD;
    }

    [[nodiscard]] bool turn(bool meaningful) {
        if (!active() || !meaningful) return false;
        state_ |= TURNED;
        return true;
    }

    [[nodiscard]] Release release() {
        if (!active()) return Release::NONE;
        const Release result = turned()
            ? Release::TURN
            : (state_ & HELD) != 0U ? Release::HOLD : Release::TAP;
        cancel();
        return result;
    }

    void cancel() {
        state_ = 0U;
    }

    [[nodiscard]] bool active() const { return (state_ & PRESSED) != 0U; }
    [[nodiscard]] bool turned() const { return (state_ & TURNED) != 0U; }

private:
    static constexpr uint8_t PRESSED = 1U << 0U;
    static constexpr uint8_t HELD = 1U << 1U;
    static constexpr uint8_t TURNED = 1U << 2U;
    uint8_t state_ = 0U;
};

static_assert(sizeof(PressHoldTurnReleaseGesture) == 1U);

}  // namespace core::handler

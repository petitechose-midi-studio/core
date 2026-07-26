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
        pressed_ = true;
        held_ = false;
        turned_ = false;
    }

    void hold() {
        if (pressed_) held_ = true;
    }

    [[nodiscard]] bool turn(bool meaningful) {
        if (!pressed_ || !meaningful) return false;
        turned_ = true;
        return true;
    }

    [[nodiscard]] Release release() {
        if (!pressed_) return Release::NONE;
        const Release result = turned_
            ? Release::TURN
            : held_ ? Release::HOLD : Release::TAP;
        cancel();
        return result;
    }

    void cancel() {
        pressed_ = false;
        held_ = false;
        turned_ = false;
    }

    [[nodiscard]] bool active() const { return pressed_; }
    [[nodiscard]] bool turned() const { return turned_; }

private:
    bool pressed_ = false;
    bool held_ = false;
    bool turned_ = false;
};

static_assert(sizeof(PressHoldTurnReleaseGesture) <= 3U);

}  // namespace core::handler

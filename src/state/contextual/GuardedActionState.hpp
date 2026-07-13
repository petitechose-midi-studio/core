#pragma once

#include <cstdint>

namespace core::state::contextual {

enum class GuardedActionPhase : uint8_t {
    IDLE = 0,
    PRESSED,
    ARMED,
    COMMITTED,
    CANCELLED,
};

enum class GuardedActionRelease : uint8_t {
    NONE = 0,
    TAP,
    CANCELLED,
    COMMITTED,
};

struct GuardedActionState {
    static constexpr uint16_t COMPLETE_PERMILLE = 1000;

    GuardedActionPhase phase = GuardedActionPhase::IDLE;
    uint32_t pressedAtMs = 0;
    uint32_t armedAtMs = 0;
    uint16_t guardDurationMs = 0;
    uint16_t progressPermille = 0;
};

constexpr bool operator==(
    const GuardedActionState& lhs,
    const GuardedActionState& rhs
) {
    return lhs.phase == rhs.phase && lhs.pressedAtMs == rhs.pressedAtMs &&
           lhs.armedAtMs == rhs.armedAtMs &&
           lhs.guardDurationMs == rhs.guardDurationMs &&
           lhs.progressPermille == rhs.progressPermille;
}

constexpr bool operator!=(
    const GuardedActionState& lhs,
    const GuardedActionState& rhs
) {
    return !(lhs == rhs);
}

void resetGuardedAction(GuardedActionState& state);

bool beginGuardedActionPress(
    GuardedActionState& state,
    uint32_t nowMs,
    uint16_t guardDurationMs
);

/** Starts guarded progress. A zero-duration guard commits immediately. */
bool armGuardedAction(GuardedActionState& state, uint32_t nowMs);

/** Returns true when phase or visible progress changed. */
bool updateGuardedAction(GuardedActionState& state, uint32_t nowMs);

/**
 * Classifies release deterministically. A release at or beyond the deadline
 * commits even if no periodic update happened on that exact tick.
 */
GuardedActionRelease releaseGuardedAction(
    GuardedActionState& state,
    uint32_t nowMs
);

bool cancelGuardedAction(GuardedActionState& state);

bool guardedActionTerminal(const GuardedActionState& state);

}  // namespace core::state::contextual

#include "state/contextual/GuardedActionState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::contextual {

namespace {

FLASHMEM uint32_t elapsedSince(uint32_t nowMs, uint32_t startedAtMs) {
    return nowMs - startedAtMs;
}

}  // namespace

FLASHMEM void resetGuardedAction(GuardedActionState& state) {
    state = GuardedActionState{};
}

FLASHMEM bool beginGuardedActionPress(
    GuardedActionState& state,
    uint32_t nowMs,
    uint16_t guardDurationMs
) {
    if (state.phase != GuardedActionPhase::IDLE) {
        return false;
    }

    state.phase = GuardedActionPhase::PRESSED;
    state.pressedAtMs = nowMs;
    state.armedAtMs = 0;
    state.guardDurationMs = guardDurationMs;
    state.progressPermille = 0;
    return true;
}

FLASHMEM bool armGuardedAction(GuardedActionState& state, uint32_t nowMs) {
    if (state.phase != GuardedActionPhase::PRESSED) {
        return false;
    }

    state.armedAtMs = nowMs;
    state.progressPermille = 0;
    if (state.guardDurationMs == 0) {
        state.phase = GuardedActionPhase::COMMITTED;
        state.progressPermille = GuardedActionState::COMPLETE_PERMILLE;
    } else {
        state.phase = GuardedActionPhase::ARMED;
    }
    return true;
}

FLASHMEM bool updateGuardedAction(GuardedActionState& state, uint32_t nowMs) {
    if (state.phase != GuardedActionPhase::ARMED) {
        return false;
    }

    const uint32_t elapsed = elapsedSince(nowMs, state.armedAtMs);
    const uint16_t previousProgress = state.progressPermille;
    if (elapsed >= state.guardDurationMs) {
        state.phase = GuardedActionPhase::COMMITTED;
        state.progressPermille = GuardedActionState::COMPLETE_PERMILLE;
        return true;
    }

    state.progressPermille = static_cast<uint16_t>(
        (static_cast<uint64_t>(elapsed) *
         GuardedActionState::COMPLETE_PERMILLE) /
        state.guardDurationMs
    );
    return state.progressPermille != previousProgress;
}

FLASHMEM GuardedActionRelease releaseGuardedAction(
    GuardedActionState& state,
    uint32_t nowMs
) {
    if (state.phase == GuardedActionPhase::PRESSED) {
        resetGuardedAction(state);
        return GuardedActionRelease::TAP;
    }

    if (state.phase == GuardedActionPhase::ARMED) {
        updateGuardedAction(state, nowMs);
        if (state.phase == GuardedActionPhase::COMMITTED) {
            return GuardedActionRelease::COMMITTED;
        }
        state.phase = GuardedActionPhase::CANCELLED;
        return GuardedActionRelease::CANCELLED;
    }

    if (state.phase == GuardedActionPhase::COMMITTED) {
        return GuardedActionRelease::COMMITTED;
    }

    return GuardedActionRelease::NONE;
}

FLASHMEM bool cancelGuardedAction(GuardedActionState& state) {
    if (state.phase != GuardedActionPhase::PRESSED &&
        state.phase != GuardedActionPhase::ARMED) {
        return false;
    }

    state.phase = GuardedActionPhase::CANCELLED;
    return true;
}

FLASHMEM bool guardedActionTerminal(const GuardedActionState& state) {
    return state.phase == GuardedActionPhase::COMMITTED ||
           state.phase == GuardedActionPhase::CANCELLED;
}

}  // namespace core::state::contextual

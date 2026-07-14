#include "state/contextual/GuardedActionState.hpp"

namespace core::state::contextual {

namespace {

uint32_t elapsedSince(uint32_t nowMs, uint32_t startedAtMs) {
    return nowMs - startedAtMs;
}

}  // namespace

void resetGuardedAction(GuardedActionState& state) {
    state = GuardedActionState{};
}

bool beginGuardedActionPress(
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

bool armGuardedAction(GuardedActionState& state, uint32_t nowMs) {
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

bool updateGuardedAction(GuardedActionState& state, uint32_t nowMs) {
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

GuardedActionRelease releaseGuardedAction(
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

bool cancelGuardedAction(GuardedActionState& state) {
    if (state.phase != GuardedActionPhase::PRESSED &&
        state.phase != GuardedActionPhase::ARMED) {
        return false;
    }

    state.phase = GuardedActionPhase::CANCELLED;
    return true;
}

bool guardedActionTerminal(const GuardedActionState& state) {
    return state.phase == GuardedActionPhase::COMMITTED ||
           state.phase == GuardedActionPhase::CANCELLED;
}

}  // namespace core::state::contextual

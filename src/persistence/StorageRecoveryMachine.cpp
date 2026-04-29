#include "persistence/StorageRecoveryMachine.hpp"

namespace core::persistence {

StorageRecoveryMachine::StorageRecoveryMachine(StorageRecoveryConfig config)
    : config_(config) {}

StorageRecoveryAction StorageRecoveryMachine::update(StorageRecoveryInput input) {
    switch (state_) {
        case StorageRecoveryState::READY:
            if (!input.mediaPresent) {
                enter_(StorageRecoveryState::MISSING_DEBOUNCE, input.nowMs);
            }
            return StorageRecoveryAction::NONE;

        case StorageRecoveryState::MISSING_DEBOUNCE:
            if (input.mediaPresent) {
                enter_(StorageRecoveryState::READY, input.nowMs);
                return StorageRecoveryAction::NONE;
            }
            if (elapsed_(input.nowMs, state_since_ms_, config_.removalDebounceMs)) {
                enter_(StorageRecoveryState::OFFLINE, input.nowMs);
                return StorageRecoveryAction::MARK_OFFLINE;
            }
            return StorageRecoveryAction::NONE;

        case StorageRecoveryState::OFFLINE:
            if (input.mediaPresent) {
                enter_(StorageRecoveryState::RECOVERY_PENDING, input.nowMs);
            }
            return StorageRecoveryAction::NONE;

        case StorageRecoveryState::RECOVERY_PENDING:
            if (!input.mediaPresent) {
                enter_(StorageRecoveryState::OFFLINE, input.nowMs);
                return StorageRecoveryAction::NONE;
            }
            if (!elapsed_(input.nowMs, state_since_ms_, config_.insertionDebounceMs)) {
                return StorageRecoveryAction::NONE;
            }
            if (input.playing) {
                return StorageRecoveryAction::NONE;
            }
            enter_(StorageRecoveryState::REOPENING, input.nowMs);
            return StorageRecoveryAction::ATTEMPT_REOPEN;

        case StorageRecoveryState::READY_RECOVERED:
            enter_(StorageRecoveryState::READY, input.nowMs);
            return StorageRecoveryAction::NONE;

        case StorageRecoveryState::DEGRADED:
            if (!input.mediaPresent) {
                enter_(StorageRecoveryState::OFFLINE, input.nowMs);
                return StorageRecoveryAction::NONE;
            }
            if (input.playing ||
                !elapsed_(input.nowMs, state_since_ms_, config_.retryBackoffMs)) {
                return StorageRecoveryAction::NONE;
            }
            enter_(StorageRecoveryState::REOPENING, input.nowMs);
            return StorageRecoveryAction::ATTEMPT_REOPEN;

        case StorageRecoveryState::REOPENING:
        case StorageRecoveryState::REVALIDATING:
        default:
            return StorageRecoveryAction::NONE;
    }
}

StorageRecoveryAction StorageRecoveryMachine::completeReopen(bool success, uint32_t nowMs) {
    if (state_ != StorageRecoveryState::REOPENING) {
        return StorageRecoveryAction::NONE;
    }
    enter_(success ? StorageRecoveryState::REVALIDATING : StorageRecoveryState::DEGRADED, nowMs);
    return success ? StorageRecoveryAction::ATTEMPT_REVALIDATE : StorageRecoveryAction::NONE;
}

StorageRecoveryAction StorageRecoveryMachine::completeRevalidation(bool success, uint32_t nowMs) {
    if (state_ != StorageRecoveryState::REVALIDATING) {
        return StorageRecoveryAction::NONE;
    }
    enter_(success ? StorageRecoveryState::READY_RECOVERED : StorageRecoveryState::DEGRADED, nowMs);
    return success ? StorageRecoveryAction::MARK_RECOVERED : StorageRecoveryAction::NONE;
}

void StorageRecoveryMachine::resetReady() {
    state_ = StorageRecoveryState::READY;
    state_since_ms_ = 0;
}

bool StorageRecoveryMachine::elapsed_(uint32_t nowMs, uint32_t sinceMs, uint32_t delayMs) {
    return static_cast<uint32_t>(nowMs - sinceMs) >= delayMs;
}

void StorageRecoveryMachine::enter_(StorageRecoveryState state, uint32_t nowMs) {
    state_ = state;
    state_since_ms_ = nowMs;
}

}  // namespace core::persistence

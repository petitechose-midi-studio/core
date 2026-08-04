#pragma once

#include <cstdint>

namespace core::persistence {

enum class StorageRecoveryState : uint8_t {
    READY,
    MISSING_DEBOUNCE,
    OFFLINE,
    RECOVERY_PENDING,
    REOPENING,
    REVALIDATING,
    READY_RECOVERED,
    DEGRADED,
};

enum class StorageRecoveryAction : uint8_t {
    NONE,
    MARK_OFFLINE,
    ATTEMPT_REOPEN,
    ATTEMPT_REVALIDATE,
    MARK_RECOVERED,
};

struct StorageRecoveryConfig {
    uint32_t removalDebounceMs = 1000;
    uint32_t insertionDebounceMs = 1000;
    uint32_t retryBackoffMs = 5000;
};

struct StorageRecoveryInput {
    bool mediaPresent = true;
    bool playing = false;
    bool reconciliationRequired = false;
    uint32_t nowMs = 0;
};

/**
 * @brief Pure media recovery state machine for SD removal/reinsert handling.
 *
 * This class owns only timing/state transitions. Platform code remains
 * responsible for sampling media presence, reopening concrete backends, and
 * asking CoreState to persist RAM-authoritative snapshots after reopen.
 */
class StorageRecoveryMachine {
public:
    explicit StorageRecoveryMachine(StorageRecoveryConfig config = {});

    StorageRecoveryAction update(StorageRecoveryInput input);
    StorageRecoveryAction completeReopen(bool success, uint32_t nowMs);
    StorageRecoveryAction completeRevalidation(bool success, uint32_t nowMs);

    StorageRecoveryState state() const { return state_; }
    void resetReady();

private:
    static bool elapsed_(uint32_t nowMs, uint32_t sinceMs, uint32_t delayMs);
    void enter_(StorageRecoveryState state, uint32_t nowMs);

    StorageRecoveryConfig config_{};
    StorageRecoveryState state_ = StorageRecoveryState::READY;
    uint32_t state_since_ms_ = 0;
};

}  // namespace core::persistence

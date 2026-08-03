#include <cassert>
#include <iostream>

#include "../../src/persistence/StorageRecoveryMachine.hpp"

namespace {

using core::persistence::StorageRecoveryAction;
using core::persistence::StorageRecoveryConfig;
using core::persistence::StorageRecoveryInput;
using core::persistence::StorageRecoveryMachine;
using core::persistence::StorageRecoveryState;

StorageRecoveryConfig fastConfig() {
    return StorageRecoveryConfig{
        .removalDebounceMs = 100,
        .insertionDebounceMs = 200,
        .retryBackoffMs = 500,
    };
}

void test_observed_transient_absence_still_requires_reconciliation() {
    StorageRecoveryMachine transient{fastConfig()};

    assert(transient.update({.mediaPresent = false, .playing = false, .nowMs = 10}) ==
           StorageRecoveryAction::NONE);
    assert(transient.state() == StorageRecoveryState::MISSING_DEBOUNCE);

    assert(transient.update({.mediaPresent = true, .playing = false, .nowMs = 50}) ==
           StorageRecoveryAction::NONE);
    assert(transient.state() == StorageRecoveryState::RECOVERY_PENDING);
    assert(transient.update({.mediaPresent = true, .playing = false, .nowMs = 249}) ==
           StorageRecoveryAction::NONE);
    assert(transient.update({.mediaPresent = true, .playing = false, .nowMs = 250}) ==
           StorageRecoveryAction::ATTEMPT_REOPEN);

    StorageRecoveryMachine sustained{fastConfig()};

    assert(sustained.update({.mediaPresent = false, .playing = false, .nowMs = 100}) ==
           StorageRecoveryAction::NONE);
    assert(sustained.update({.mediaPresent = false, .playing = false, .nowMs = 199}) ==
           StorageRecoveryAction::NONE);
    assert(sustained.state() == StorageRecoveryState::MISSING_DEBOUNCE);

    assert(sustained.update({.mediaPresent = false, .playing = false, .nowMs = 200}) ==
           StorageRecoveryAction::MARK_OFFLINE);
    assert(sustained.state() == StorageRecoveryState::OFFLINE);

    std::cout
        << "[PASS] test_observed_transient_absence_still_requires_reconciliation\n";
}

void test_external_reconciliation_requirement_uses_retry_backoff() {
    StorageRecoveryMachine machine{fastConfig()};

    assert(machine.update({
        .mediaPresent = true,
        .playing = false,
        .reconciliationRequired = true,
        .nowMs = 10,
    }) == StorageRecoveryAction::NONE);
    assert(machine.state() == StorageRecoveryState::DEGRADED);
    assert(machine.update({
        .mediaPresent = true,
        .playing = false,
        .reconciliationRequired = true,
        .nowMs = 509,
    }) == StorageRecoveryAction::NONE);
    assert(machine.update({
        .mediaPresent = true,
        .playing = false,
        .reconciliationRequired = true,
        .nowMs = 510,
    }) == StorageRecoveryAction::ATTEMPT_REOPEN);

    std::cout
        << "[PASS] test_external_reconciliation_requirement_uses_retry_backoff\n";
}

void test_reinsert_waits_until_idle_before_reopen() {
    StorageRecoveryMachine machine{fastConfig()};

    assert(machine.update({.mediaPresent = false, .playing = false, .nowMs = 0}) ==
           StorageRecoveryAction::NONE);
    assert(machine.update({.mediaPresent = false, .playing = false, .nowMs = 100}) ==
           StorageRecoveryAction::MARK_OFFLINE);

    assert(machine.update({.mediaPresent = true, .playing = false, .nowMs = 150}) ==
           StorageRecoveryAction::NONE);
    assert(machine.state() == StorageRecoveryState::RECOVERY_PENDING);

    assert(machine.update({.mediaPresent = true, .playing = true, .nowMs = 400}) ==
           StorageRecoveryAction::NONE);
    assert(machine.state() == StorageRecoveryState::RECOVERY_PENDING);

    assert(machine.update({.mediaPresent = true, .playing = false, .nowMs = 401}) ==
           StorageRecoveryAction::ATTEMPT_REOPEN);
    assert(machine.state() == StorageRecoveryState::REOPENING);

    std::cout << "[PASS] test_reinsert_waits_until_idle_before_reopen\n";
}

void test_reopen_and_revalidation_success_marks_recovered_then_ready() {
    StorageRecoveryMachine machine{fastConfig()};

    machine.update({.mediaPresent = false, .playing = false, .nowMs = 0});
    machine.update({.mediaPresent = false, .playing = false, .nowMs = 100});
    machine.update({.mediaPresent = true, .playing = false, .nowMs = 200});
    assert(machine.update({.mediaPresent = true, .playing = false, .nowMs = 400}) ==
           StorageRecoveryAction::ATTEMPT_REOPEN);

    assert(machine.completeReopen(true, 410) == StorageRecoveryAction::ATTEMPT_REVALIDATE);
    assert(machine.state() == StorageRecoveryState::REVALIDATING);

    assert(machine.completeRevalidation(true, 420) == StorageRecoveryAction::MARK_RECOVERED);
    assert(machine.state() == StorageRecoveryState::READY_RECOVERED);

    assert(machine.update({.mediaPresent = true, .playing = false, .nowMs = 430}) ==
           StorageRecoveryAction::NONE);
    assert(machine.state() == StorageRecoveryState::READY);

    std::cout << "[PASS] test_reopen_and_revalidation_success_marks_recovered_then_ready\n";
}

void test_failed_recovery_retries_with_backoff() {
    StorageRecoveryMachine machine{fastConfig()};

    machine.update({.mediaPresent = false, .playing = false, .nowMs = 0});
    machine.update({.mediaPresent = false, .playing = false, .nowMs = 100});
    machine.update({.mediaPresent = true, .playing = false, .nowMs = 200});
    assert(machine.update({.mediaPresent = true, .playing = false, .nowMs = 400}) ==
           StorageRecoveryAction::ATTEMPT_REOPEN);

    assert(machine.completeReopen(false, 450) == StorageRecoveryAction::NONE);
    assert(machine.state() == StorageRecoveryState::DEGRADED);

    assert(machine.update({.mediaPresent = true, .playing = false, .nowMs = 949}) ==
           StorageRecoveryAction::NONE);
    assert(machine.update({.mediaPresent = true, .playing = false, .nowMs = 950}) ==
           StorageRecoveryAction::ATTEMPT_REOPEN);
    assert(machine.state() == StorageRecoveryState::REOPENING);

    std::cout << "[PASS] test_failed_recovery_retries_with_backoff\n";
}

void test_media_removed_during_pending_recovery_returns_offline() {
    StorageRecoveryMachine machine{fastConfig()};

    machine.update({.mediaPresent = false, .playing = false, .nowMs = 0});
    machine.update({.mediaPresent = false, .playing = false, .nowMs = 100});
    machine.update({.mediaPresent = true, .playing = false, .nowMs = 200});
    assert(machine.state() == StorageRecoveryState::RECOVERY_PENDING);

    assert(machine.update({.mediaPresent = false, .playing = false, .nowMs = 250}) ==
           StorageRecoveryAction::NONE);
    assert(machine.state() == StorageRecoveryState::OFFLINE);

    std::cout << "[PASS] test_media_removed_during_pending_recovery_returns_offline\n";
}

}  // namespace

int main() {
    test_observed_transient_absence_still_requires_reconciliation();
    test_external_reconciliation_requirement_uses_retry_backoff();
    test_reinsert_waits_until_idle_before_reopen();
    test_reopen_and_revalidation_success_marks_recovered_then_ready();
    test_failed_recovery_retries_with_backoff();
    test_media_removed_during_pending_recovery_returns_offline();

    std::cout << "All StorageRecoveryMachine tests passed\n";
    return 0;
}

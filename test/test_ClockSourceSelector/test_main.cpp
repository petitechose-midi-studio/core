#include <cassert>
#include <iostream>

#include "../../src/sequencer/ClockSourceSelector.hpp"

namespace {

using core::state::MidiSyncMode;

void test_auto_locks_after_configured_clock_count() {
    core::sequencer::ClockSourceSelector selector;

    auto result = selector.update(MidiSyncMode::AUTO, 100, 0, 0);
    assert(!result.useExternal);
    assert(!result.externalSignal);

    selector.recordClock(MidiSyncMode::AUTO, 3);
    selector.recordClock(MidiSyncMode::AUTO, 3);
    selector.recordClock(MidiSyncMode::AUTO, 3);

    result = selector.update(MidiSyncMode::AUTO, 100, 30, 30);
    assert(result.useExternal);
    assert(result.externalSignal);
    assert(!result.resetExternalTempo);

    std::cout << "[PASS] test_auto_locks_after_configured_clock_count\n";
}

void test_auto_falls_back_after_signal_timeout() {
    core::sequencer::ClockSourceSelector selector;

    selector.recordClock(MidiSyncMode::AUTO, 1);
    auto result = selector.update(MidiSyncMode::AUTO, 100, 10, 10);
    assert(result.useExternal);

    result = selector.update(MidiSyncMode::AUTO, 100, 200, 10);
    assert(!result.useExternal);
    assert(!result.externalSignal);
    assert(result.resetExternalTempo);

    std::cout << "[PASS] test_auto_falls_back_after_signal_timeout\n";
}

void test_slave_uses_external_even_before_signal() {
    core::sequencer::ClockSourceSelector selector;

    const auto result = selector.update(MidiSyncMode::SLAVE, 100, 0, 0);
    assert(result.useExternal);
    assert(!result.externalSignal);
    assert(result.resetExternalTempo);
    assert(selector.locked());

    std::cout << "[PASS] test_slave_uses_external_even_before_signal\n";
}

void test_master_clears_lock() {
    core::sequencer::ClockSourceSelector selector;

    selector.recordClock(MidiSyncMode::AUTO, 1);
    assert(selector.locked());

    const auto result = selector.update(MidiSyncMode::MASTER, 100, 10, 10);
    assert(!result.useExternal);
    assert(!selector.locked());
    assert(result.resetExternalTempo);

    std::cout << "[PASS] test_master_clears_lock\n";
}

}  // namespace

int main() {
    test_auto_locks_after_configured_clock_count();
    test_auto_falls_back_after_signal_timeout();
    test_slave_uses_external_even_before_signal();
    test_master_clears_lock();

    std::cout << "All ClockSourceSelector tests passed\n";
    return 0;
}

#include <array>
#include <cassert>
#include <iostream>

#include "../../src/context/standalone/StandaloneSequencerRuntimeGate.hpp"

namespace {

using core::context::standalone::StandaloneSequencerRuntimeAction;
using core::context::standalone::StandaloneSequencerRuntimeDecision;

constexpr bool decisionMatches(
    const StandaloneSequencerRuntimeDecision& lhs,
    const StandaloneSequencerRuntimeDecision& rhs
) {
    return lhs.action == rhs.action &&
           lhs.nextWasStandaloneActive == rhs.nextWasStandaloneActive;
}

void test_inactive_context_keeps_runtime_idle_when_already_inactive() {
    constexpr auto decision =
        core::context::standalone::decideStandaloneSequencerRuntimeAction(false, false);
    constexpr StandaloneSequencerRuntimeDecision expected{
        StandaloneSequencerRuntimeAction::NONE,
        false,
    };

    static_assert(decisionMatches(decision, expected));
    assert(decisionMatches(decision, expected));
    std::cout << "[PASS] test_inactive_context_keeps_runtime_idle_when_already_inactive\n";
}

void test_entering_standalone_updates_runtime_and_marks_gate_active() {
    constexpr auto decision =
        core::context::standalone::decideStandaloneSequencerRuntimeAction(true, false);
    constexpr StandaloneSequencerRuntimeDecision expected{
        StandaloneSequencerRuntimeAction::UPDATE,
        true,
    };

    static_assert(decisionMatches(decision, expected));
    assert(decisionMatches(decision, expected));
    std::cout << "[PASS] test_entering_standalone_updates_runtime_and_marks_gate_active\n";
}

void test_staying_in_standalone_keeps_runtime_updating() {
    constexpr auto decision =
        core::context::standalone::decideStandaloneSequencerRuntimeAction(true, true);
    constexpr StandaloneSequencerRuntimeDecision expected{
        StandaloneSequencerRuntimeAction::UPDATE,
        true,
    };

    static_assert(decisionMatches(decision, expected));
    assert(decisionMatches(decision, expected));
    std::cout << "[PASS] test_staying_in_standalone_keeps_runtime_updating\n";
}

void test_leaving_standalone_stops_runtime_once_and_clears_gate() {
    constexpr auto decision =
        core::context::standalone::decideStandaloneSequencerRuntimeAction(false, true);
    constexpr StandaloneSequencerRuntimeDecision expected{
        StandaloneSequencerRuntimeAction::STOP,
        false,
    };

    static_assert(decisionMatches(decision, expected));
    assert(decisionMatches(decision, expected));
    std::cout << "[PASS] test_leaving_standalone_stops_runtime_once_and_clears_gate\n";
}

}  // namespace

int main() {
    test_inactive_context_keeps_runtime_idle_when_already_inactive();
    test_entering_standalone_updates_runtime_and_marks_gate_active();
    test_staying_in_standalone_keeps_runtime_updating();
    test_leaving_standalone_stops_runtime_once_and_clears_gate();
    std::cout << "\nAll standalone sequencer runtime gate tests passed.\n";
    return 0;
}

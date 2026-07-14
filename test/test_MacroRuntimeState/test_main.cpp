#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>

#include "../../src/state/macro/MacroRuntimeState.hpp"

namespace {

namespace macro = core::state::macro;

bool near(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

macro::MacroAutomationSlotAddress addressAt(uint8_t index) {
    return macro::MacroAutomationSlotAddress{
        .track = static_cast<uint8_t>(index / macro::MACRO_COUNT),
        .page = 0,
        .macro = static_cast<uint8_t>(index % macro::MACRO_COUNT),
    };
}

void test_manual_override_is_keyed_by_full_slot_address() {
    macro::MacroManualOverrideState state;
    const auto first = macro::MacroAutomationSlotAddress{.track = 1, .page = 2, .macro = 3};
    const auto sameMacroOtherPage =
        macro::MacroAutomationSlotAddress{.track = 1, .page = 4, .macro = 3};

    assert(state.activate(first, 0.25f) ==
           macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(state.activate(sameMacroOtherPage, 0.75f) ==
           macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(state.entryCount == 2);
    float value = 0.0f;
    assert(state.valueFor(first, value));
    assert(near(value, 0.25f));
    assert(state.valueFor(sameMacroOtherPage, value));
    assert(near(value, 0.75f));

    const uint32_t revision = state.revision;
    assert(state.activate(first, 0.25f) ==
           macro::MacroManualOverrideState::ActivateStatus::UNCHANGED);
    assert(state.revision == revision);
    assert(state.activate(first, 2.0f) ==
           macro::MacroManualOverrideState::ActivateStatus::UPDATED);
    assert(state.valueFor(first, value));
    assert(near(value, 1.0f));

    assert(state.resume(first));
    assert(!state.activeFor(first));
    assert(state.activeFor(sameMacroOtherPage));
    assert(!state.resume(first));

    std::cout << "[PASS] test_manual_override_is_keyed_by_full_slot_address\n";
}

void test_manual_override_capacity_never_evicts_and_reports_rejection() {
    macro::MacroManualOverrideState state;
    for (uint8_t i = 0; i < macro::MacroManualOverrideState::CAPACITY; ++i) {
        assert(state.activate(addressAt(i), static_cast<float>(i) / 63.0f) ==
               macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    }
    assert(state.entryCount == macro::MacroManualOverrideState::CAPACITY);
    const auto first = addressAt(0);
    float firstValue = 0.0f;
    assert(state.valueFor(first, firstValue));

    const auto overflow = macro::MacroAutomationSlotAddress{.track = 8, .page = 1, .macro = 0};
    assert(state.activate(overflow, 0.5f) ==
           macro::MacroManualOverrideState::ActivateStatus::CAPACITY_EXHAUSTED);
    assert(state.rejectedActivationCount == 1);
    assert(state.entryCount == macro::MacroManualOverrideState::CAPACITY);
    float retained = 0.0f;
    assert(state.valueFor(first, retained));
    assert(near(retained, firstValue));
    assert(!state.activeFor(overflow));

    state.rejectedActivationCount = std::numeric_limits<uint32_t>::max();
    assert(state.activate(overflow, 0.5f) ==
           macro::MacroManualOverrideState::ActivateStatus::CAPACITY_EXHAUSTED);
    assert(state.rejectedActivationCount == std::numeric_limits<uint32_t>::max());

    std::cout
        << "[PASS] test_manual_override_capacity_never_evicts_and_reports_rejection\n";
}

void test_project_boundary_clear_preserves_diagnostic_counter() {
    macro::MacroManualOverrideState state;
    const auto address = addressAt(0);
    assert(state.activate(address, 0.5f) ==
           macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(state.activate(
               macro::MacroAutomationSlotAddress{.track = macro::TRACK_COUNT},
               0.5f
           ) == macro::MacroManualOverrideState::ActivateStatus::INVALID_ADDRESS);
    const uint32_t revision = state.revision;

    state.clearProjectRuntime();
    assert(state.entryCount == 0);
    assert(!state.activeFor(address));
    assert(state.revision == revision + 1U);
    assert(state.rejectedActivationCount == 1);
    state.resetTelemetry();
    assert(state.rejectedActivationCount == 0);

    std::cout << "[PASS] test_project_boundary_clear_preserves_diagnostic_counter\n";
}

void test_snapshot_is_bounded_and_stable_after_capture() {
    macro::MacroManualOverrideState state;
    const auto first = addressAt(0);
    const auto second = addressAt(1);
    assert(state.activate(first, 0.25f) ==
           macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(state.activate(second, 0.75f) ==
           macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);

    macro::MacroManualOverrideState::Snapshot snapshot{};
    assert(state.captureSnapshot(snapshot));
    assert(snapshot.revision == state.revision);
    assert(snapshot.entryCount == 2);
    assert(snapshot.entries[0].active);
    assert(macro::macroAutomationAddressEquals(snapshot.entries[0].address, first));
    assert(near(snapshot.entries[0].value, 0.25f));
    assert(macro::macroAutomationAddressEquals(snapshot.entries[1].address, second));
    assert(near(snapshot.entries[1].value, 0.75f));

    assert(state.activate(first, 0.5f) ==
           macro::MacroManualOverrideState::ActivateStatus::UPDATED);
    assert(snapshot.revision != state.revision);
    assert(near(snapshot.entries[0].value, 0.25f));

    macro::MacroManualOverrideState::Snapshot updated{};
    assert(state.captureSnapshot(updated));
    assert(updated.revision == state.revision);
    assert(near(updated.entries[0].value, 0.5f));

    std::cout << "[PASS] test_snapshot_is_bounded_and_stable_after_capture\n";
}

void test_scope_clear_removes_only_replaced_addresses() {
    macro::MacroManualOverrideState state;
    const auto pageTarget = macro::MacroAutomationSlotAddress{.track = 1, .page = 2, .macro = 0};
    const auto samePage = macro::MacroAutomationSlotAddress{.track = 1, .page = 2, .macro = 4};
    const auto sameTrackOtherPage =
        macro::MacroAutomationSlotAddress{.track = 1, .page = 3, .macro = 0};
    const auto otherTrack = macro::MacroAutomationSlotAddress{.track = 2, .page = 2, .macro = 0};
    assert(state.activate(pageTarget, 0.1f) ==
           macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(state.activate(samePage, 0.2f) ==
           macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(state.activate(sameTrackOtherPage, 0.3f) ==
           macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);
    assert(state.activate(otherTrack, 0.4f) ==
           macro::MacroManualOverrideState::ActivateStatus::ACTIVATED);

    const uint32_t beforePageClear = state.revision;
    assert(state.clearPage(1, 2) == 2);
    assert(state.revision == beforePageClear + 1U);
    assert(!state.activeFor(pageTarget));
    assert(!state.activeFor(samePage));
    assert(state.activeFor(sameTrackOtherPage));
    assert(state.activeFor(otherTrack));
    assert(state.entryCount == 2);

    const uint32_t beforeTrackClear = state.revision;
    assert(state.clearTrack(1) == 1);
    assert(state.revision == beforeTrackClear + 1U);
    assert(!state.activeFor(sameTrackOtherPage));
    assert(state.activeFor(otherTrack));
    assert(state.entryCount == 1);
    assert(state.clearPage(macro::TRACK_COUNT, 0) == 0);
    assert(state.clearTrack(macro::TRACK_COUNT) == 0);

    std::cout << "[PASS] test_scope_clear_removes_only_replaced_addresses\n";
}

}  // namespace

int main() {
    test_manual_override_is_keyed_by_full_slot_address();
    test_manual_override_capacity_never_evicts_and_reports_rejection();
    test_project_boundary_clear_preserves_diagnostic_counter();
    test_snapshot_is_bounded_and_stable_after_capture();
    test_scope_clear_removes_only_replaced_addresses();
    std::cout << "\nAll MacroRuntimeState tests passed.\n";
    return 0;
}

#include <cassert>
#include <cstdint>
#include <iostream>

#include "state/macro/MacroAutomationTake.hpp"

namespace {

using core::state::macro::MacroAutomationTakePhase;
using core::state::macro::MacroAutomationTakeState;
using core::state::macro::MacroAutomationTakeTiming;
using core::state::macro::MacroPackedCurvePoint;

std::array<uint8_t, 8> bases() {
    return {10U, 20U, 30U, 40U, 50U, 60U, 70U, 80U};
}

void test_timing_table_and_wrap_are_musical() {
    using namespace core::state::macro;
    assert(macroAutomationTakeFixedDurationTicks(
        MacroAutomationTakeTiming::NOTE_1_16) == 48U);
    assert(macroAutomationTakeFixedDurationTicks(
        MacroAutomationTakeTiming::BARS_32) == 24576U);
    assert(nextMacroAutomationTakeTiming(
        MacroAutomationTakeTiming::HOLD, -1) ==
        MacroAutomationTakeTiming::BARS_32);
    assert(macroAutomationTakeQuantizeHoldTicks(55U) == 48U);
    assert(macroAutomationTakeQuantizeHoldTicks(1100U) == 768U);
    assert(macroAutomationTakeQuantizeHoldTicks(2000U) == 2304U);
    std::cout << "[PASS] timing table and wrap\n";
}

void test_late_macro_join_holds_its_initial_value_from_t0() {
    MacroAutomationTakeState take{};
    take.arm(MacroAutomationTakeTiming::BAR_1, 0x03U, bases());
    assert(take.begin(1000U, 500U, 125U, 4U, 9U));
    assert(take.touch(0U, 64U, 48U));
    assert(take.sample(96U));
    assert(take.touch(1U, 100U, 192U));
    assert(take.finish(768U));

    std::array<MacroPackedCurvePoint, 2048> points{};
    uint16_t count = 0U;
    assert(take.buildPackedCurve(1U, points.data(), points.size(), count));
    assert(count >= 2U);
    assert(points[0].tick == 0U);
    assert(points[0].value < points[count - 1U].value);
    assert(points[count - 1U].tick == 768U);
    assert(take.touchedMask == 0x03U);
    assert(take.durationTicks == 768U);
    std::cout << "[PASS] late join holds t0 base\n";
}

void test_all_columns_are_decimated_on_the_same_time_axis() {
    MacroAutomationTakeState take{};
    take.arm(MacroAutomationTakeTiming::BARS_32, 0x03U, bases());
    assert(take.begin(0U, 0U, 0U, 1U, 1U));
    for (uint32_t tick = 1U; tick < 2400U; ++tick) {
        assert(take.touch(0U, static_cast<uint8_t>(tick % 128U), tick));
        if ((tick % 7U) == 0U) {
            assert(take.touch(1U, static_cast<uint8_t>((tick * 3U) % 128U), tick));
        }
    }
    assert(take.reduced);
    assert(take.sampleCount < MacroAutomationTakeState::SAMPLE_CAPACITY);
    for (uint16_t index = 1U; index < take.sampleCount; ++index) {
        assert(take.elapsedTicks[index] > take.elapsedTicks[index - 1U]);
    }
    std::cout << "[PASS] shared decimation\n";
}

void test_fixed_take_clamps_and_phase_offset_replays_t0() {
    MacroAutomationTakeState take{};
    take.arm(MacroAutomationTakeTiming::NOTE_1_4, 0x01U, bases());
    assert(take.begin(0U, 1234U, 50U, 2U, 3U));
    assert(take.touch(0U, 90U, 250U));
    assert(take.completeAt(250U));
    assert(take.finish(250U));
    assert(take.durationTicks == 192U);
    assert(take.elapsedTicks[take.sampleCount - 1U] == 192U);
    assert(take.playbackWindowOffsetTicks() == 142U);
    std::cout << "[PASS] fixed clamp and phase\n";
}

void test_linear_midi_motion_is_simplified_without_losing_endpoints() {
    MacroAutomationTakeState take{};
    take.arm(MacroAutomationTakeTiming::BAR_1, 0x01U, bases());
    assert(take.begin(0U, 0U, 0U, 1U, 1U));
    for (uint16_t index = 0U; index <= 127U; ++index) {
        const uint16_t tick = static_cast<uint16_t>(
            (static_cast<uint32_t>(index) * 768U + 63U) / 127U
        );
        assert(take.touch(0U, static_cast<uint8_t>(index), tick));
    }
    assert(take.finish(768U));
    std::array<MacroPackedCurvePoint, 2048> points{};
    uint16_t count = 0U;
    assert(take.buildPackedCurve(0U, points.data(), points.size(), count));
    assert(count == 2U);
    assert(points[0].tick == 0U);
    assert(points[1].tick == 768U);
    assert(points[0].value == 0U);
    assert(points[1].value == 32767);
    std::cout << "[PASS] MIDI-7 simplification\n";
}

void test_untouched_columns_are_not_materialized() {
    MacroAutomationTakeState take{};
    take.arm(MacroAutomationTakeTiming::HOLD, 0xFFU, bases());
    assert(take.begin(0U, 0U, 0U, 1U, 1U));
    assert(take.touch(3U, 99U, 100U));
    assert(take.finish(100U));
    std::array<MacroPackedCurvePoint, 2048> points{};
    uint16_t count = 0U;
    assert(!take.buildPackedCurve(2U, points.data(), points.size(), count));
    assert(take.buildPackedCurve(3U, points.data(), points.size(), count));
    assert(take.phase == MacroAutomationTakePhase::RECORDING);
    std::cout << "[PASS] untouched byte identity boundary\n";
}

}  // namespace

int main() {
    test_timing_table_and_wrap_are_musical();
    test_late_macro_join_holds_its_initial_value_from_t0();
    test_all_columns_are_decimated_on_the_same_time_axis();
    test_fixed_take_clamps_and_phase_offset_replays_t0();
    test_linear_midi_motion_is_simplified_without_losing_endpoints();
    test_untouched_columns_are_not_materialized();
    std::cout << "\nAll MacroAutomationTake tests passed.\n";
    return 0;
}

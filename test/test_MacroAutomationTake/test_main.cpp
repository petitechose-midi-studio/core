#include <cassert>
#include <cstdint>
#include <iostream>

#include "state/macro/MacroAutomationTake.hpp"

namespace {

using core::state::macro::MacroAutomationTakePhase;
using core::state::macro::MacroAutomationTakeState;
using core::state::macro::MacroAutomationTakeTiming;
using core::state::modulation::ProjectPackedCurvePoint;

std::array<uint8_t, 8> bases() {
    return {10U, 20U, 30U, 40U, 50U, 60U, 70U, 80U};
}

uint8_t gridValueAt(
    const MacroAutomationTakeState& take,
    uint8_t macro,
    uint16_t tick
) {
    for (uint16_t sample = 0U; sample < take.sampleCount; ++sample) {
        if (take.elapsedTicks[sample] == tick) return take.values[macro][sample];
    }
    assert(false && "requested tick must exist in this fixed test grid");
    return 0U;
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

void test_late_macro_join_preserves_prefill_before_its_first_movement() {
    MacroAutomationTakeState take{};
    take.arm(MacroAutomationTakeTiming::BAR_1, 0x03U, bases());
    assert(take.begin(1000U, 500U, 125U, 4U, 9U));
    assert(take.touch(0U, 64U, 48U));
    assert(take.sample(96U));
    assert(take.touch(1U, 100U, 192U));
    assert(take.finish(768U));

    std::array<ProjectPackedCurvePoint, 2048> points{};
    uint16_t count = 0U;
    assert(take.buildPackedCurve(1U, points.data(), points.size(), count));
    assert(count >= 2U);
    assert(points[0].tick == 0U);
    assert(points[count - 1U].tick == 768U);
    assert(gridValueAt(take, 1U, 192U) == 20U);
    assert(gridValueAt(take, 1U, 384U) == 100U);
    assert(take.touchedMask == 0x03U);
    assert(take.durationTicks == 768U);
    std::cout << "[PASS] late join holds t0 base\n";
}

void test_long_fixed_grid_is_bounded_on_one_shared_time_axis() {
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
    assert(take.sampleCount == MacroAutomationTakeState::SAMPLE_CAPACITY);
    for (uint16_t index = 1U; index < take.sampleCount; ++index) {
        assert(take.elapsedTicks[index] > take.elapsedTicks[index - 1U]);
    }
    std::cout << "[PASS] shared decimation\n";
}

void test_fixed_take_wraps_in_project_phase_without_terminal_completion() {
    MacroAutomationTakeState take{};
    take.arm(MacroAutomationTakeTiming::NOTE_1_4, 0x01U, bases());
    assert(take.begin(0U, 1234U, 50U, 2U, 3U));
    assert(take.touch(0U, 90U, 250U));
    assert(take.finish(250U));
    assert(take.durationTicks == 192U);
    assert(take.elapsedTicks[take.sampleCount - 1U] == 192U);
    assert(take.playbackWindowOffsetTicks() == 0U);
    // Project phase 50 + elapsed 250 wraps to phase 108.
    assert(gridValueAt(take, 0U, 108U) == 90U);
    assert(take.phase == MacroAutomationTakePhase::RECORDING);
    std::cout << "[PASS] fixed circular project phase\n";
}

void test_latest_fixed_pass_wins_and_phase_zero_is_seamless() {
    MacroAutomationTakeState take{};
    take.arm(MacroAutomationTakeTiming::NOTE_1_4, 0x01U, bases());
    assert(take.begin(0U, 0U, 48U, 1U, 1U));
    assert(take.touch(0U, 40U, 0U));       // project phase 48
    const uint32_t firstCurveRevision = take.scratchCurveRevision;
    assert(firstCurveRevision > 0U);
    assert(take.sample(0U));
    assert(take.scratchCurveRevision == firstCurveRevision);
    float previewValue = 0.0f;
    assert(take.sampleFixedPreviewValue(0U, 16384U, previewValue));
    assert(previewValue > 0.31f && previewValue < 0.32f);
    assert(take.sample(192U));             // one complete pass
    assert(take.touch(0U, 100U, 240U));    // next pass, project phase 96
    assert(take.sample(432U));             // keep recording beyond two loops
    assert(take.phase == MacroAutomationTakePhase::RECORDING);
    assert(gridValueAt(take, 0U, 96U) == 100U);
    assert(take.values[0U][0U] ==
           take.values[0U][take.sampleCount - 1U]);
    std::cout << "[PASS] latest circular pass wins\n";
}

void test_linear_midi_motion_is_simplified_without_losing_endpoints() {
    MacroAutomationTakeState take{};
    take.arm(MacroAutomationTakeTiming::HOLD, 0x01U, bases());
    assert(take.begin(0U, 0U, 0U, 1U, 1U));
    for (uint16_t index = 0U; index <= 127U; ++index) {
        const uint16_t tick = static_cast<uint16_t>(
            (static_cast<uint32_t>(index) * 768U + 63U) / 127U
        );
        assert(take.touch(0U, static_cast<uint8_t>(index), tick));
    }
    assert(take.finish(768U));
    std::array<ProjectPackedCurvePoint, 2048> points{};
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
    std::array<ProjectPackedCurvePoint, 2048> points{};
    uint16_t count = 0U;
    assert(!take.buildPackedCurve(2U, points.data(), points.size(), count));
    assert(take.buildPackedCurve(3U, points.data(), points.size(), count));
    assert(take.phase == MacroAutomationTakePhase::RECORDING);
    std::cout << "[PASS] untouched byte identity boundary\n";
}

}  // namespace

int main() {
    test_timing_table_and_wrap_are_musical();
    test_late_macro_join_preserves_prefill_before_its_first_movement();
    test_long_fixed_grid_is_bounded_on_one_shared_time_axis();
    test_fixed_take_wraps_in_project_phase_without_terminal_completion();
    test_latest_fixed_pass_wins_and_phase_zero_is_seamless();
    test_linear_midi_motion_is_simplified_without_losing_endpoints();
    test_untouched_columns_are_not_materialized();
    std::cout << "\nAll MacroAutomationTake tests passed.\n";
    return 0;
}

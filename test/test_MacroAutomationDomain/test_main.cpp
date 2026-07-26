#include <cassert>
#include <cmath>
#include <iostream>

#include "state/macro/MacroAutomationDomain.hpp"

namespace {

bool near(float lhs, float rhs, float epsilon = 0.0001f) {
    return std::fabs(lhs - rhs) <= epsilon;
}

void test_duration_quantization_uses_bounded_musical_values() {
    using namespace core::state::macro;

    assert(near(macroAutomationQuantizeDurationBeats(0.33f), 0.25f));
    assert(near(macroAutomationQuantizeDurationBeats(0.76f), 1.0f));
    assert(near(macroAutomationQuantizeDurationBeats(3.3f), 4.0f));
    assert(near(macroAutomationQuantizeDurationBeats(11.0f), 12.0f));

    std::cout << "[PASS] test_duration_quantization_uses_bounded_musical_values\n";
}

void test_elapsed_beats_uses_tempo_and_safe_defaults() {
    using namespace core::state::macro;

    assert(near(macroAutomationElapsedBeats(1000, 1500, 120.0f), 1.0f));
    assert(near(macroAutomationElapsedBeats(1000, 1500, 60.0f), 0.5f));
    assert(near(macroAutomationElapsedBeats(1000, 1500, 0.0f), 1.0f));
    assert(near(macroAutomationElapsedBeats(1500, 1000, 120.0f), 0.0f));
    assert(near(macroAutomationElapsedBeats(0xFFFF'FFF0U, 0x0000'0010U, 120.0f), 0.064f));

    std::cout << "[PASS] test_elapsed_beats_uses_tempo_and_safe_defaults\n";
}

void test_finalize_recording_remaps_points_to_quantized_duration() {
    using namespace core::state::macro;

    MacroAutomationLane lane;
    assert(macroAutomationAppendPoint(lane, 0.0f, 0.25f));
    assert(macroAutomationAppendPoint(lane, 0.75f, 0.75f));

    macroAutomationFinalizeRecording(lane, 1.5f);

    assert(lane.active);
    assert(near(lane.durationBeats, 2.0f));
    assert(lane.pointCount == 2);
    assert(near(lane.points[0].beat, 0.0f));
    assert(near(lane.points[1].beat, 1.0f));
    assert(near(lane.points[1].value, 0.75f));

    std::cout << "[PASS] test_finalize_recording_remaps_points_to_quantized_duration\n";
}

void test_finalize_recording_simplifies_dense_linear_motion() {
    using namespace core::state::macro;

    MacroAutomationLane lane;
    for (uint16_t i = 0; i <= 64; ++i) {
        const float beat = static_cast<float>(i) / 64.0f;
        assert(macroAutomationAppendPoint(lane, beat, beat));
    }

    macroAutomationFinalizeRecording(lane, 1.0f);

    assert(lane.active);
    assert(near(lane.durationBeats, 1.0f));
    assert(lane.pointCount == 2);
    assert(near(lane.points[0].beat, 0.0f));
    assert(near(lane.points[0].value, 0.0f));
    assert(near(lane.points[1].beat, 1.0f));
    assert(near(lane.points[1].value, 1.0f));
    assert(near(macroAutomationEvaluate(lane, 0.5f, 0.0f), 0.5f));

    std::cout << "[PASS] test_finalize_recording_simplifies_dense_linear_motion\n";
}

void test_recording_reduces_dense_input_instead_of_truncating() {
    using namespace core::state::macro;

    MacroAutomationLane lane;
    for (uint16_t i = 0; i < MACRO_AUTOMATION_RECORDING_MAX_POINTS; ++i) {
        const float value = (i & 1U) == 0U ? 0.0f : 1.0f;
        assert(macroAutomationAppendPoint(lane, static_cast<float>(i), value));
    }

    bool reduced = false;
    assert(macroAutomationAppendPoint(
        lane,
        static_cast<float>(MACRO_AUTOMATION_RECORDING_MAX_POINTS),
        0.5f,
        &reduced
    ));
    assert(reduced);
    assert(lane.pointCount < MACRO_AUTOMATION_RECORDING_MAX_POINTS);
    assert(near(lane.points[0].beat, 0.0f));
    assert(near(
        lane.points[static_cast<uint16_t>(lane.pointCount - 1U)].beat,
        static_cast<float>(MACRO_AUTOMATION_RECORDING_MAX_POINTS)
    ));

    std::cout << "[PASS] test_recording_reduces_dense_input_instead_of_truncating\n";
}

void test_finalize_recording_preserves_audible_turning_points() {
    using namespace core::state::macro;

    MacroAutomationLane lane;
    assert(macroAutomationAppendPoint(lane, 0.0f, 0.0f));
    assert(macroAutomationAppendPoint(lane, 0.25f, 0.5f));
    assert(macroAutomationAppendPoint(lane, 0.5f, 1.0f));
    assert(macroAutomationAppendPoint(lane, 0.75f, 0.5f));
    assert(macroAutomationAppendPoint(lane, 1.0f, 0.0f));

    macroAutomationFinalizeRecording(lane, 1.0f);

    assert(lane.active);
    assert(lane.pointCount == 3);
    assert(near(lane.points[0].beat, 0.0f));
    assert(near(lane.points[0].value, 0.0f));
    assert(near(lane.points[1].beat, 0.5f));
    assert(near(lane.points[1].value, 1.0f));
    assert(near(lane.points[2].beat, 1.0f));
    assert(near(lane.points[2].value, 0.0f));

    std::cout << "[PASS] test_finalize_recording_preserves_audible_turning_points\n";
}

void test_finalize_recording_coalesces_same_tick_and_constant_tail() {
    using namespace core::state::macro;

    MacroAutomationLane lane;
    assert(macroAutomationAppendPoint(lane, 0.0f, 0.1f));
    assert(macroAutomationAppendPoint(lane, 0.001f, 0.8f));
    assert(macroAutomationAppendPoint(lane, 0.5f, 0.8f));

    macroAutomationFinalizeRecording(lane, 1.0f);

    assert(lane.active);
    // The last value written on the snapped tick is authoritative. Because the
    // remainder is constant, one point is the exact and most compact lane.
    assert(lane.pointCount == 1);
    assert(near(lane.points[0].beat, 0.0f));
    assert(near(lane.points[0].value, 0.8f));

    std::cout << "[PASS] test_finalize_recording_coalesces_same_tick_and_constant_tail\n";
}

void test_linear_interpolation_and_tail_hold_are_deterministic() {
    using namespace core::state::macro;

    MacroAutomationLane lane;
    lane.durationBeats = 4.0f;
    assert(macroAutomationAppendPoint(lane, 0.0f, 0.0f));
    assert(macroAutomationAppendPoint(lane, 2.0f, 1.0f));

    assert(near(macroAutomationEvaluate(lane, 0.0f, 0.5f), 0.0f));
    assert(near(macroAutomationEvaluate(lane, 1.0f, 0.5f), 0.5f));
    assert(near(macroAutomationEvaluate(lane, 2.0f, 0.5f), 1.0f));
    assert(near(macroAutomationEvaluate(lane, 3.0f, 0.5f), 1.0f));
    assert(near(macroAutomationEvaluate(lane, 5.0f, 0.5f), 0.5f));

    std::cout << "[PASS] test_linear_interpolation_and_tail_hold_are_deterministic\n";
}

void test_loop_boundary_keeps_start_point_authoritative() {
    using namespace core::state::macro;

    MacroAutomationLane lane;
    lane.durationBeats = 4.0f;
    assert(macroAutomationAppendPoint(lane, 0.0f, 0.2f));
    assert(macroAutomationAppendPoint(lane, 4.0f, 0.8f));

    assert(near(macroAutomationEvaluate(lane, 0.0f, 0.0f), 0.2f));
    assert(near(macroAutomationEvaluate(lane, 4.0f, 0.0f), 0.2f));
    assert(near(macroAutomationEvaluate(lane, 3.0f, 0.0f), 0.65f));

    std::cout << "[PASS] test_loop_boundary_keeps_start_point_authoritative\n";
}

void test_points_must_be_appended_in_musical_order() {
    using namespace core::state::macro;

    MacroAutomationLane lane;
    assert(macroAutomationAppendPoint(lane, 1.0f, 0.4f));
    assert(!macroAutomationAppendPoint(lane, 0.5f, 0.6f));
    assert(lane.pointCount == 1);

    std::cout << "[PASS] test_points_must_be_appended_in_musical_order\n";
}

}  // namespace

int main() {
    test_duration_quantization_uses_bounded_musical_values();
    test_elapsed_beats_uses_tempo_and_safe_defaults();
    test_finalize_recording_remaps_points_to_quantized_duration();
    test_finalize_recording_simplifies_dense_linear_motion();
    test_recording_reduces_dense_input_instead_of_truncating();
    test_finalize_recording_preserves_audible_turning_points();
    test_finalize_recording_coalesces_same_tick_and_constant_tail();
    test_linear_interpolation_and_tail_hold_are_deterministic();
    test_loop_boundary_keeps_start_point_authoritative();
    test_points_must_be_appended_in_musical_order();

    std::cout << "\nAll MacroAutomationDomain tests passed.\n";
    return 0;
}

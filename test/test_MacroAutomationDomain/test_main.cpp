#include <cassert>
#include <cmath>
#include <iostream>

#include "state/macro/MacroAutomationDomain.hpp"
#include "state/macro/MacroAutomationState.hpp"

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

void test_finalize_recording_coalesces_same_tick_with_last_value() {
    using namespace core::state::macro;

    MacroAutomationLane lane;
    assert(macroAutomationAppendPoint(lane, 0.0f, 0.1f));
    assert(macroAutomationAppendPoint(lane, 0.001f, 0.8f));
    assert(macroAutomationAppendPoint(lane, 0.5f, 0.8f));

    macroAutomationFinalizeRecording(lane, 1.0f);

    assert(lane.active);
    assert(lane.pointCount == 2);
    assert(near(lane.points[0].beat, 0.0f));
    assert(near(lane.points[0].value, 0.8f));
    assert(near(lane.points[1].beat, 0.5f));
    assert(near(lane.points[1].value, 0.8f));

    std::cout << "[PASS] test_finalize_recording_coalesces_same_tick_with_last_value\n";
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

    MacroModulationShape shape;
    assert(macroModulationAppendPoint(shape, 1.0f, 0.2f));
    assert(!macroModulationAppendPoint(shape, 0.5f, -0.2f));
    assert(shape.pointCount == 1);

    std::cout << "[PASS] test_points_must_be_appended_in_musical_order\n";
}

void test_automation_conversion_policies_make_relative_shapes() {
    using namespace core::state::macro;

    MacroAutomationLane lane;
    lane.durationBeats = 4.0f;
    assert(macroAutomationAppendPoint(lane, 0.0f, 0.25f));
    assert(macroAutomationAppendPoint(lane, 2.0f, 0.75f));

    MacroModulationShape meanShape;
    assert(macroAutomationConvertToModulation(
        lane,
        MacroAutomationConversionPolicy::MEAN,
        meanShape
    ));
    assert(near(meanShape.points[0].value, -0.25f));
    assert(near(meanShape.points[1].value, 0.25f));

    MacroModulationShape firstShape;
    assert(macroAutomationConvertToModulation(
        lane,
        MacroAutomationConversionPolicy::FIRST,
        firstShape
    ));
    assert(near(firstShape.points[0].value, 0.0f));
    assert(near(firstShape.points[1].value, 0.5f));

    MacroModulationShape minShape;
    assert(macroAutomationConvertToModulation(
        lane,
        MacroAutomationConversionPolicy::MIN,
        minShape
    ));
    assert(near(minShape.points[0].value, 0.0f));
    assert(near(minShape.points[1].value, 0.5f));

    std::cout << "[PASS] test_automation_conversion_policies_make_relative_shapes\n";
}

void test_resolve_static_automation_modulation_and_depth() {
    using namespace core::state::macro;

    MacroAutomationBankState bank;
    auto* motion = macroAutomationGetOrCreateSlot(
        bank,
        MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 0}
    );
    assert(motion != nullptr);
    MacroAutomationLane automation;
    automation.durationBeats = 4.0f;
    assert(macroAutomationAppendPoint(automation, 0.0f, 0.2f));
    assert(macroAutomationAppendPoint(automation, 2.0f, 0.8f));
    assert(macroAutomationAssignAutomation(bank, *motion, automation));

    MacroModulationShape modulation;
    modulation.durationBeats = 4.0f;
    assert(macroModulationAppendPoint(modulation, 0.0f, -0.5f));
    assert(macroModulationAppendPoint(modulation, 2.0f, 0.5f));
    assert(macroAutomationAssignModulation(bank, *motion, modulation));
    motion->modulationDepth = 0.2f;

    const auto atStart = macroResolveValue(0.6f, *motion, bank.pointPool, 0.0f);
    assert(atStart.automationActive);
    assert(atStart.modulationActive);
    assert(near(atStart.base, 0.2f));
    assert(near(atStart.modulation, -0.1f));
    assert(near(atStart.resolved, 0.1f));

    const auto atMiddle = macroResolveValue(0.6f, *motion, bank.pointPool, 2.0f);
    assert(near(atMiddle.base, 0.8f));
    assert(near(atMiddle.modulation, 0.1f));
    assert(near(atMiddle.resolved, 0.9f));

    MacroAutomationSlotState noMotion;
    const auto staticOnly = macroResolveValue(0.6f, noMotion, bank.pointPool, 1.0f);
    assert(!staticOnly.automationActive);
    assert(!staticOnly.modulationActive);
    assert(near(staticOnly.base, 0.6f));
    assert(near(staticOnly.resolved, 0.6f));

    std::cout << "[PASS] test_resolve_static_automation_modulation_and_depth\n";
}

void test_persisted_curve_duration_resize_uses_non_destructive_window() {
    using namespace core::state::macro;

    MacroAutomationBankState bank;
    auto* slot = macroAutomationGetOrCreateSlot(
        bank,
        MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 0}
    );
    assert(slot != nullptr);

    MacroAutomationLane lane;
    lane.durationBeats = 2.0f;
    assert(macroAutomationAppendPoint(lane, 0.0f, 0.25f));
    assert(macroAutomationAppendPoint(lane, 1.0f, 0.75f));
    assert(macroAutomationAppendPoint(lane, 2.0f, 0.25f));
    assert(macroAutomationAssignAutomation(bank, *slot, lane));

    assert(macroAutomationResizeCurveDuration(slot->automation, bank.pointPool, 4.0f));
    assert(near(macroAutomationBeatsFromTicks(slot->automation.durationTicks), 4.0f));
    assert(near(macroAutomationBeatsFromTicks(slot->automation.sourceDurationTicks), 2.0f));
    assert(slot->automation.pointCount == 3);

    MacroCurvePoint middle{};
    assert(macroAutomationReadPoint(slot->automation, bank.pointPool, 1, false, middle));
    assert(near(middle.beat, 1.0f));
    assert(near(middle.value, 0.75f));

    assert(near(macroAutomationEvaluate(
        slot->automation,
        bank.pointPool,
        3.0f,
        0.0f
    ), 0.75f));

    assert(macroAutomationResizeCurveDuration(slot->automation, bank.pointPool, 0.5f));
    assert(near(macroAutomationBeatsFromTicks(slot->automation.durationTicks), 0.5f));
    assert(slot->automation.pointCount == 3);

    MacroCurvePoint first{};
    assert(macroAutomationReadPoint(slot->automation, bank.pointPool, 0, false, first));
    assert(near(first.beat, 0.0f));
    assert(near(first.value, 0.25f));

    assert(macroAutomationResizeCurveDuration(slot->automation, bank.pointPool, 2.0f));
    assert(near(macroAutomationEvaluate(
        slot->automation,
        bank.pointPool,
        1.0f,
        0.0f
    ), 0.75f));

    assert(macroAutomationResizeCurveDuration(slot->automation, bank.pointPool, 1.0f));
    assert(macroAutomationSetCurveWindowOffset(slot->automation, bank.pointPool, 1.0f));
    assert(near(macroAutomationEvaluate(
        slot->automation,
        bank.pointPool,
        0.0f,
        0.0f
    ), 0.75f));
    assert(near(macroAutomationEvaluate(
        slot->automation,
        bank.pointPool,
        0.5f,
        0.0f
    ), 0.5f));
    assert(macroAutomationSetCurveWindowOffset(slot->automation, bank.pointPool, 2.0f));
    assert(near(macroAutomationBeatsFromTicks(slot->automation.windowOffsetTicks), 0.0f));

    std::cout << "[PASS] test_persisted_curve_duration_resize_uses_non_destructive_window\n";
}

void test_curve_window_summary_matches_persisted_window_semantics() {
    using namespace core::state::macro;

    MacroAutomationBankState bank;
    MacroAutomationCurveRef inactive;
    const auto emptySummary = macroAutomationCurveWindowSummary(inactive, bank.pointPool);
    assert(!emptySummary.active);

    auto* slot = macroAutomationGetOrCreateSlot(
        bank,
        MacroAutomationSlotAddress{.track = 0, .page = 0, .macro = 0}
    );
    assert(slot != nullptr);

    MacroAutomationLane lane;
    lane.durationBeats = 2.0f;
    assert(macroAutomationAppendPoint(lane, 0.0f, 0.25f));
    assert(macroAutomationAppendPoint(lane, 1.0f, 0.75f));
    assert(macroAutomationAppendPoint(lane, 2.0f, 0.25f));
    assert(macroAutomationAssignAutomation(bank, *slot, lane));

    assert(macroAutomationSetCurveWindowOffset(slot->automation, bank.pointPool, 1.0f));

    const auto summary = macroAutomationCurveWindowSummary(
        slot->automation,
        bank.pointPool
    );
    assert(summary.active);
    assert(summary.pointCount == 3);
    assert(near(macroAutomationBeatsFromTicks(summary.sourceDurationTicks), 2.0f));
    assert(near(macroAutomationBeatsFromTicks(summary.durationTicks), 2.0f));
    assert(near(macroAutomationBeatsFromTicks(summary.windowOffsetTicks), 1.0f));
    assert(near(macroAutomationBeatsFromTicks(summary.firstPointTick), 0.0f));
    assert(near(macroAutomationBeatsFromTicks(summary.lastPointTick), 2.0f));
    assert(summary.wraps);

    assert(macroAutomationSetCurveWindowOffset(slot->automation, bank.pointPool, 0.0f));
    assert(macroAutomationSetCurveWindowOffset(slot->automation, bank.pointPool, 3.0f));
    const auto wrappedOffset = macroAutomationCurveWindowSummary(
        slot->automation,
        bank.pointPool
    );
    assert(near(macroAutomationBeatsFromTicks(wrappedOffset.windowOffsetTicks), 1.0f));

    std::cout << "[PASS] test_curve_window_summary_matches_persisted_window_semantics\n";
}

}  // namespace

int main() {
    test_duration_quantization_uses_bounded_musical_values();
    test_elapsed_beats_uses_tempo_and_safe_defaults();
    test_finalize_recording_remaps_points_to_quantized_duration();
    test_finalize_recording_simplifies_dense_linear_motion();
    test_recording_reduces_dense_input_instead_of_truncating();
    test_finalize_recording_preserves_audible_turning_points();
    test_finalize_recording_coalesces_same_tick_with_last_value();
    test_linear_interpolation_and_tail_hold_are_deterministic();
    test_loop_boundary_keeps_start_point_authoritative();
    test_points_must_be_appended_in_musical_order();
    test_automation_conversion_policies_make_relative_shapes();
    test_resolve_static_automation_modulation_and_depth();
    test_persisted_curve_duration_resize_uses_non_destructive_window();
    test_curve_window_summary_matches_persisted_window_semantics();

    std::cout << "\nAll MacroAutomationDomain tests passed.\n";
    return 0;
}

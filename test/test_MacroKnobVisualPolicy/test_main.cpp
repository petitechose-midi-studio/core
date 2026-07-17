#include <cassert>
#include <iostream>

#include "ui/widget/MacroKnobVisualPolicy.hpp"

namespace {

using namespace core::ui::macro_knob_visual;

void test_signed_span_preserves_base_and_output_endpoints() {
    const auto positive = modulationSpan(200, 260);
    assert(positive.start == 200);
    assert(positive.end == 260);

    const auto negative = modulationSpan(260, 200);
    assert(negative.start == 200);
    assert(negative.end == 260);

    std::cout << "[PASS] signed rail span preserves both endpoints\n";
}

void test_zero_delta_mark_stays_inside_the_knob_sweep() {
    const auto middle = modulationSpan(220, 220);
    assert(middle.start == 220);
    assert(middle.end == 220 + IDLE_MARK_DEGREES);

    const auto upperBound = modulationSpan(END_ANGLE, END_ANGLE);
    assert(upperBound.start == END_ANGLE - IDLE_MARK_DEGREES);
    assert(upperBound.end == END_ANGLE);

    std::cout << "[PASS] zero-delta mark stays inside the sweep\n";
}

void test_three_pixel_rail_has_no_visual_gutter() {
    static_assert(MODULATION_RAIL_WIDTH == 3);
    static_assert(modulationRailInvalidationWidth() == 5);

    constexpr uint16_t baseRadius = 24;
    constexpr int16_t oddBaseWidth = 5;
    constexpr uint16_t oddRailRadius =
        modulationRailRadius(baseRadius, oddBaseWidth);
    static_assert(
        2 * oddRailRadius - MODULATION_RAIL_WIDTH ==
        2 * baseRadius + oddBaseWidth
    );

    constexpr int16_t evenBaseWidth = 6;
    constexpr uint16_t evenRailRadius =
        modulationRailRadius(baseRadius, evenBaseWidth);
    static_assert(
        2 * evenRailRadius - MODULATION_RAIL_WIDTH <=
        2 * baseRadius + evenBaseWidth
    );

    std::cout << "[PASS] three-pixel rail touches the Base arc\n";
}

void test_hot_invalidation_targets_only_changed_trajectories() {
    auto plan = resolvedInvalidationPlan(
        true, 200, 240, false, 210, 250, false
    );
    assert(plan.mainArcChanged);
    assert(plan.previousMainAngle == 200);
    assert(plan.nextMainAngle == 210);
    assert(plan.railChanged);
    assert(plan.previousRail.start == 200 && plan.previousRail.end == 240);
    assert(plan.nextRail.start == 210 && plan.nextRail.end == 250);

    plan = resolvedInvalidationPlan(
        true, 210, 250, false, 210, 250, true
    );
    assert(!plan.mainArcChanged);
    assert(plan.railChanged);

    plan = resolvedInvalidationPlan(
        false, 210, 250, false, 180, 260, true
    );
    assert(plan.mainArcChanged);
    assert(plan.previousMainAngle == 250);
    assert(plan.nextMainAngle == 260);
    assert(!plan.railChanged);

    plan = resolvedInvalidationPlan(
        true, 210, 250, true, 210, 250, true
    );
    assert(!plan.mainArcChanged);
    assert(!plan.railChanged);

    std::cout << "[PASS] hot invalidation is limited to changed trajectories\n";
}

}  // namespace

int main() {
    test_signed_span_preserves_base_and_output_endpoints();
    test_zero_delta_mark_stays_inside_the_knob_sweep();
    test_three_pixel_rail_has_no_visual_gutter();
    test_hot_invalidation_targets_only_changed_trajectories();
    std::cout << "\nAll MacroKnobVisualPolicy tests passed.\n";
    return 0;
}

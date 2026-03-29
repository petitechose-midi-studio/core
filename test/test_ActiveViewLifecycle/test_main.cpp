#include <array>
#include <cassert>
#include <iostream>

#include "../../src/context/standalone/ActiveViewLifecyclePlan.hpp"

namespace {

using core::context::standalone::ActiveViewLifecycleStep;

template <size_t N>
constexpr bool plansMatch(const std::array<ActiveViewLifecycleStep, N>& lhs,
                          const std::array<ActiveViewLifecycleStep, N>& rhs) {
    for (size_t i = 0; i < N; ++i) {
        if (lhs[i] != rhs[i]) return false;
    }
    return true;
}

void test_macro_view_lifecycle_plan_orders_deactivate_then_activate_then_sync() {
    constexpr auto plan =
        core::context::standalone::makeActiveViewLifecyclePlan(core::ui::ViewType::MACRO);

    constexpr std::array<ActiveViewLifecycleStep, 4> expected{
        ActiveViewLifecycleStep::DEACTIVATE_MACRO,
        ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::ACTIVATE_MACRO,
        ActiveViewLifecycleStep::SYNC_MACRO_ENCODERS,
    };

    static_assert(plansMatch(plan, expected));
    assert(plansMatch(plan, expected));
    std::cout
        << "[PASS] test_macro_view_lifecycle_plan_orders_deactivate_then_activate_then_sync\n";
}

void test_sequencer_view_lifecycle_plan_orders_deactivate_then_activate_then_sync() {
    constexpr auto plan =
        core::context::standalone::makeActiveViewLifecyclePlan(core::ui::ViewType::SEQUENCER);

    constexpr std::array<ActiveViewLifecycleStep, 4> expected{
        ActiveViewLifecycleStep::DEACTIVATE_MACRO,
        ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::ACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::SYNC_SEQUENCER_ENCODERS,
    };

    static_assert(plansMatch(plan, expected));
    assert(plansMatch(plan, expected));
    std::cout
        << "[PASS] test_sequencer_view_lifecycle_plan_orders_deactivate_then_activate_then_sync\n";
}

void test_unknown_view_falls_back_to_macro_lifecycle_plan() {
    constexpr auto plan = core::context::standalone::makeActiveViewLifecyclePlan(
        static_cast<core::ui::ViewType>(255)
    );

    constexpr std::array<ActiveViewLifecycleStep, 4> expected{
        ActiveViewLifecycleStep::DEACTIVATE_MACRO,
        ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::ACTIVATE_MACRO,
        ActiveViewLifecycleStep::SYNC_MACRO_ENCODERS,
    };

    static_assert(plansMatch(plan, expected));
    assert(plansMatch(plan, expected));
    std::cout << "[PASS] test_unknown_view_falls_back_to_macro_lifecycle_plan\n";
}

}  // namespace

int main() {
    test_macro_view_lifecycle_plan_orders_deactivate_then_activate_then_sync();
    test_sequencer_view_lifecycle_plan_orders_deactivate_then_activate_then_sync();
    test_unknown_view_falls_back_to_macro_lifecycle_plan();
    std::cout << "\nAll active-view lifecycle tests passed.\n";
    return 0;
}

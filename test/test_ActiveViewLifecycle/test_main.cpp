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

    constexpr std::array<ActiveViewLifecycleStep, 6> expected{
        ActiveViewLifecycleStep::DEACTIVATE_MACRO,
        ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::DEACTIVATE_PROJECT,
        ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS,
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

    constexpr std::array<ActiveViewLifecycleStep, 6> expected{
        ActiveViewLifecycleStep::DEACTIVATE_MACRO,
        ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::DEACTIVATE_PROJECT,
        ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS,
        ActiveViewLifecycleStep::ACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::SYNC_SEQUENCER_ENCODERS,
    };

    static_assert(plansMatch(plan, expected));
    assert(plansMatch(plan, expected));
    std::cout
        << "[PASS] test_sequencer_view_lifecycle_plan_orders_deactivate_then_activate_then_sync\n";
}

void test_project_view_lifecycle_plan_orders_deactivate_then_activate_then_sync() {
    constexpr auto plan =
        core::context::standalone::makeActiveViewLifecyclePlan(core::ui::ViewType::PROJECT);

    constexpr std::array<ActiveViewLifecycleStep, 6> expected{
        ActiveViewLifecycleStep::DEACTIVATE_MACRO,
        ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::DEACTIVATE_PROJECT,
        ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS,
        ActiveViewLifecycleStep::ACTIVATE_PROJECT,
        ActiveViewLifecycleStep::SYNC_PROJECT_ENCODER,
    };

    static_assert(plansMatch(plan, expected));
    assert(plansMatch(plan, expected));
    std::cout
        << "[PASS] test_project_view_lifecycle_plan_orders_deactivate_then_activate_then_sync\n";
}

void test_modulators_view_reuses_retained_project_lifecycle() {
    constexpr auto plan =
        core::context::standalone::makeActiveViewLifecyclePlan(
            core::ui::ViewType::MODULATORS
        );

    constexpr std::array<ActiveViewLifecycleStep, 6> expected{
        ActiveViewLifecycleStep::DEACTIVATE_MACRO,
        ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::DEACTIVATE_PROJECT,
        ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS,
        ActiveViewLifecycleStep::ACTIVATE_PROJECT,
        ActiveViewLifecycleStep::SYNC_PROJECT_ENCODER,
    };

    static_assert(plansMatch(plan, expected));
    assert(plansMatch(plan, expected));
    std::cout << "[PASS] test_modulators_view_reuses_retained_project_lifecycle\n";
}

void test_device_settings_view_lifecycle_plan_orders_deactivate_then_activate() {
    constexpr auto plan = core::context::standalone::makeActiveViewLifecyclePlan(
        core::ui::ViewType::DEVICE_SETTINGS
    );

    constexpr std::array<ActiveViewLifecycleStep, 6> expected{
        ActiveViewLifecycleStep::DEACTIVATE_MACRO,
        ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::DEACTIVATE_PROJECT,
        ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS,
        ActiveViewLifecycleStep::ACTIVATE_DEVICE_SETTINGS,
        ActiveViewLifecycleStep::NONE,
    };

    static_assert(plansMatch(plan, expected));
    assert(plansMatch(plan, expected));
    std::cout
        << "[PASS] test_device_settings_view_lifecycle_plan_orders_deactivate_then_activate\n";
}

void test_unknown_view_falls_back_to_macro_lifecycle_plan() {
    constexpr auto plan = core::context::standalone::makeActiveViewLifecyclePlan(
        static_cast<core::ui::ViewType>(255)
    );

    constexpr std::array<ActiveViewLifecycleStep, 6> expected{
        ActiveViewLifecycleStep::DEACTIVATE_MACRO,
        ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
        ActiveViewLifecycleStep::DEACTIVATE_PROJECT,
        ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS,
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
    test_project_view_lifecycle_plan_orders_deactivate_then_activate_then_sync();
    test_modulators_view_reuses_retained_project_lifecycle();
    test_device_settings_view_lifecycle_plan_orders_deactivate_then_activate();
    test_unknown_view_falls_back_to_macro_lifecycle_plan();
    std::cout << "\nAll active-view lifecycle tests passed.\n";
    return 0;
}

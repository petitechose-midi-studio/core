#pragma once

#include <array>
#include <cstdint>

#include "app/ViewTypes.hpp"

namespace core::context::standalone {

/**
 * Ordered activation side effects for standalone view changes.
 *
 * View classes stay presentation-focused; lifecycle-owned resync work such as
 * macro runtime/status refresh and encoder sync belongs to the context seam
 * that applies this plan.
 */
enum class ActiveViewLifecycleStep : uint8_t {
    NONE = 0,
    DEACTIVATE_MACRO,
    DEACTIVATE_SEQUENCER,
    DEACTIVATE_PROJECT,
    DEACTIVATE_DEVICE_SETTINGS,
    ACTIVATE_MACRO,
    ACTIVATE_SEQUENCER,
    ACTIVATE_PROJECT,
    ACTIVATE_DEVICE_SETTINGS,
    SYNC_MACRO_ENCODERS,
    SYNC_SEQUENCER_ENCODERS,
    SYNC_PROJECT_ENCODER,
};

using ActiveViewLifecyclePlan = std::array<ActiveViewLifecycleStep, 6>;

constexpr ActiveViewLifecyclePlan makeActiveViewLifecyclePlan(core::ui::ViewType activeView) {
    switch (activeView) {
        case core::ui::ViewType::SEQUENCER:
            return {
                ActiveViewLifecycleStep::DEACTIVATE_MACRO,
                ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
                ActiveViewLifecycleStep::DEACTIVATE_PROJECT,
                ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS,
                ActiveViewLifecycleStep::ACTIVATE_SEQUENCER,
                ActiveViewLifecycleStep::SYNC_SEQUENCER_ENCODERS,
            };
        case core::ui::ViewType::PROJECT:
            return {
                ActiveViewLifecycleStep::DEACTIVATE_MACRO,
                ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
                ActiveViewLifecycleStep::DEACTIVATE_PROJECT,
                ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS,
                ActiveViewLifecycleStep::ACTIVATE_PROJECT,
                ActiveViewLifecycleStep::SYNC_PROJECT_ENCODER,
            };
        case core::ui::ViewType::DEVICE_SETTINGS:
            return {
                ActiveViewLifecycleStep::DEACTIVATE_MACRO,
                ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
                ActiveViewLifecycleStep::DEACTIVATE_PROJECT,
                ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS,
                ActiveViewLifecycleStep::ACTIVATE_DEVICE_SETTINGS,
                ActiveViewLifecycleStep::NONE,
            };
        case core::ui::ViewType::MACRO:
        default:
            return {
                ActiveViewLifecycleStep::DEACTIVATE_MACRO,
                ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
                ActiveViewLifecycleStep::DEACTIVATE_PROJECT,
                ActiveViewLifecycleStep::DEACTIVATE_DEVICE_SETTINGS,
                ActiveViewLifecycleStep::ACTIVATE_MACRO,
                ActiveViewLifecycleStep::SYNC_MACRO_ENCODERS,
            };
    }
}

}  // namespace core::context::standalone

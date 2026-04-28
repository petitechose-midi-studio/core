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
    DEACTIVATE_MACRO = 0,
    DEACTIVATE_SEQUENCER,
    ACTIVATE_MACRO,
    ACTIVATE_SEQUENCER,
    SYNC_MACRO_ENCODERS,
    SYNC_SEQUENCER_ENCODERS,
};

using ActiveViewLifecyclePlan = std::array<ActiveViewLifecycleStep, 4>;

constexpr ActiveViewLifecyclePlan makeActiveViewLifecyclePlan(core::ui::ViewType activeView) {
    switch (activeView) {
        case core::ui::ViewType::SEQUENCER:
            return {
                ActiveViewLifecycleStep::DEACTIVATE_MACRO,
                ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
                ActiveViewLifecycleStep::ACTIVATE_SEQUENCER,
                ActiveViewLifecycleStep::SYNC_SEQUENCER_ENCODERS,
            };
        case core::ui::ViewType::MACRO:
        default:
            return {
                ActiveViewLifecycleStep::DEACTIVATE_MACRO,
                ActiveViewLifecycleStep::DEACTIVATE_SEQUENCER,
                ActiveViewLifecycleStep::ACTIVATE_MACRO,
                ActiveViewLifecycleStep::SYNC_MACRO_ENCODERS,
            };
    }
}

}  // namespace core::context::standalone

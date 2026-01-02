#pragma once

/**
 * @file CoreState.hpp
 * @brief Global state aggregate for standalone mode
 *
 * CoreState lives at application level (in main.cpp) and survives
 * context switches. This allows state persistence across context
 * activate/deactivate cycles.
 */

#include <oc/state/OverlayManager.hpp>

#include "MacroState.hpp"

namespace state {

/**
 * @brief Overlay types for standalone mode
 */
enum class CoreOverlayType : uint8_t {
    NONE = 0,
    PAGE_SELECTOR,
    COUNT  // Must be last
};

/**
 * @brief Global state container for standalone mode
 *
 * Unlike BitwigContext which owns its state internally,
 * StandaloneContext receives a reference to CoreState.
 * This allows state to survive context switches.
 */
struct CoreState {
    /// Runtime macro state (8 slots with values, labels)
    MacroState macros;

    /// Overlay visibility manager
    oc::state::OverlayManager<CoreOverlayType> overlays;

    CoreState() = default;

    // Non-copyable, non-movable
    CoreState(const CoreState&) = delete;
    CoreState& operator=(const CoreState&) = delete;
    CoreState(CoreState&&) = delete;
    CoreState& operator=(CoreState&&) = delete;

    /**
     * @brief Reset all state to defaults
     */
    void resetAll() {
        // Re-initialize macros with default labels
        for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
            char buf[16];
            snprintf(buf, sizeof(buf), "Macro %d", i + 1);
            macros.slots[i].label.set(buf);
            macros.slots[i].value.set(0.5f);
            macros.slots[i].updateDisplayValue();
        }
        overlays.hideAll();
    }
};

}  // namespace state

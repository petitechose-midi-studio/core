#pragma once

/**
 * @file MacroEditState.hpp
 * @brief State for macro edit overlay
 *
 * Tracks which macro is being edited and temporary values.
 */

#include <oc/state/Signal.hpp>

namespace core::state {

/**
 * @brief State for macro edit overlay
 *
 * Tracks which macro is being edited and temporary values
 * before they are saved to persistent storage.
 */
struct MacroEditState {
    /// Overlay visibility (owned by ExclusiveVisibilityStack)
    oc::state::Signal<bool> visible{false};

    /// Which macro is being edited (0-7)
    oc::state::Signal<uint8_t> editingIndex{0};

    /// Temporary channel value (0-15), not yet saved
    oc::state::Signal<uint8_t> tempChannel{0};

    /// Temporary CC value (0-127), not yet saved
    oc::state::Signal<uint8_t> tempCC{0};

    /// Focused row in overlay (0 = channel, 1 = CC)
    oc::state::Signal<uint8_t> focusedRow{0};

    /**
     * @brief Reset to defaults
     */
    void reset() {
        editingIndex.set(0);
        tempChannel.set(0);
        tempCC.set(0);
        focusedRow.set(0);
    }

    /**
     * @brief Start editing a macro
     *
     * Loads current config into temp values and shows overlay.
     *
     * @param index Macro index (0-7)
     * @param channel Current MIDI channel (0-15)
     * @param cc Current CC number (0-127)
     */
    void startEditing(uint8_t index, uint8_t channel, uint8_t cc) {
        editingIndex.set(index);
        tempChannel.set(channel);
        tempCC.set(cc);
        focusedRow.set(0);  // Start on channel
    }
};

}  // namespace core::state

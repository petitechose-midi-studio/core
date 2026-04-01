#pragma once

/**
 * @file MacroEditState.hpp
 * @brief State for macro edit overlay
 *
 * Tracks which macro is being edited and the current overlay-local edit values.
 */

#include <cstdint>

#include <oc/state/Signal.hpp>

namespace core::state {

enum class MacroEditFlowPhase : uint8_t {
    CLOSED = 0,
    EDIT = 1,
    VALUE_SELECTOR = 2,
    PAGE_SELECTOR = 3,
    TARGET_SELECTOR = 4,
};

/**
 * @brief State for macro edit overlay
 *
 * Tracks which macro is being edited and the current overlay-local edit values.
 *
 * The overlay keeps its own editable CH/CC fields so the UI can move between
 * rows, selectors, pages, and target macros without repeatedly re-reading
 * state. Those values are applied immediately when changed.
 */
struct MacroEditState {
    /// Overlay visibility (owned by ExclusiveVisibilityStack)
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<MacroEditFlowPhase, 4> flowPhase{MacroEditFlowPhase::CLOSED};

    /// Which macro is being edited (0-7)
    oc::state::Signal<uint8_t> editingIndex{0};

    /// Current editable channel value shown by the overlay (0-15)
    oc::state::Signal<uint8_t> tempChannel{0};

    /// Current editable CC value shown by the overlay (0-127)
    oc::state::Signal<uint8_t> tempCC{0};

    /// Focused row in overlay (0 = channel, 1 = CC)
    oc::state::Signal<uint8_t> focusedRow{0};

    struct ValueSelectorState {
        oc::state::Signal<bool, 4> visible{false};
        oc::state::Signal<uint8_t, 4> editingRow{0};
        oc::state::Signal<int, 4> selectedIndex{0};

        void reset() {
            visible.set(false);
            editingRow.set(0);
            selectedIndex.set(0);
        }
    };

    struct MacroSelectorState {
        oc::state::Signal<bool, 4> visible{false};
        oc::state::Signal<int, 4> selectedIndex{0};

        void reset() {
            visible.set(false);
            selectedIndex.set(0);
        }
    };

    /// Value selector sub-state (CH/CC choices)
    ValueSelectorState selector;

    /// Macro selector sub-state (macro target while editing)
    MacroSelectorState macroSelector;

    /// Runtime decision state for long-press open release policy
    uint8_t openedByMacroIndex = 0;
    uint32_t openedAtMs = 0;
    bool pendingOpenReleaseDecision = false;

    /**
     * @brief Reset to defaults
     */
    void reset() {
        visible.set(false);
        flowPhase.set(MacroEditFlowPhase::CLOSED);
        editingIndex.set(0);
        tempChannel.set(0);
        tempCC.set(0);
        focusedRow.set(0);
        selector.reset();
        macroSelector.reset();
        openedByMacroIndex = 0;
        openedAtMs = 0;
        pendingOpenReleaseDecision = false;
    }

    /**
     * @brief Start editing a macro
     *
     * Loads current config into the overlay edit fields and shows overlay.
     *
     * @param index Macro index (0-7)
     * @param channel Current MIDI channel (0-15)
     * @param cc Current CC number (0-127)
     */
    void openEditor(uint8_t index, uint8_t channel, uint8_t cc, uint32_t openedAt) {
        reset();
        visible.set(true);
        flowPhase.set(MacroEditFlowPhase::EDIT);
        editingIndex.set(index);
        tempChannel.set(channel);
        tempCC.set(cc);
        focusedRow.set(0);  // Start on channel
        openedByMacroIndex = index;
        openedAtMs = openedAt;
        pendingOpenReleaseDecision = true;
    }

    void closeEditor() {
        reset();
    }

    void openValueSelector(uint8_t row, int selectedIndex) {
        visible.set(true);
        selector.visible.set(true);
        selector.editingRow.set(row);
        selector.selectedIndex.set(selectedIndex);
        flowPhase.set(MacroEditFlowPhase::VALUE_SELECTOR);
    }

    void closeValueSelector() {
        selector.reset();
        flowPhase.set(visible.get() ? MacroEditFlowPhase::EDIT
                                    : MacroEditFlowPhase::CLOSED);
    }

    void openPageSelector() {
        visible.set(true);
        flowPhase.set(MacroEditFlowPhase::PAGE_SELECTOR);
    }

    void closePageSelector() {
        flowPhase.set(visible.get() ? MacroEditFlowPhase::EDIT
                                    : MacroEditFlowPhase::CLOSED);
    }

    void openTargetSelector(int selectedIndex) {
        visible.set(true);
        macroSelector.visible.set(true);
        macroSelector.selectedIndex.set(selectedIndex);
        flowPhase.set(MacroEditFlowPhase::TARGET_SELECTOR);
    }

    void closeTargetSelector() {
        macroSelector.reset();
        flowPhase.set(visible.get() ? MacroEditFlowPhase::EDIT
                                    : MacroEditFlowPhase::CLOSED);
    }

    void loadActiveConfig(uint8_t index, uint8_t channel, uint8_t cc) {
        editingIndex.set(index);
        tempChannel.set(channel);
        tempCC.set(cc);
    }

    bool consumeOpeningReleaseDecision(uint8_t macroIndex,
                                       uint32_t nowMs,
                                       uint32_t quickReleaseWindowMs) {
        if (!visible.get()) return false;
        if (!pendingOpenReleaseDecision) return false;
        if (macroIndex != openedByMacroIndex) return false;

        pendingOpenReleaseDecision = false;
        return (nowMs - openedAtMs) >= quickReleaseWindowMs;
    }
};

}  // namespace core::state

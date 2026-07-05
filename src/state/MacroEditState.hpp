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
    AUTOMATION = 5,
};

/**
 * @brief State for macro edit overlay
 *
 * Tracks which macro is being edited and the current overlay-local edit values.
 *
 * The overlay keeps its own editable CH/CC fields so the UI can move between
 * rows, selectors, pages, and target macros without repeatedly re-reading
 * state. Those values are committed when the editor closes or switches context.
 */
struct MacroEditState {
    /// Overlay visibility (owned by ExclusiveVisibilityStack)
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<bool, 4> automationVisible{false};
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

        void reset();
    };

    struct MacroSelectorState {
        oc::state::Signal<bool, 4> visible{false};
        oc::state::Signal<int, 4> selectedIndex{0};

        void reset();
    };

    /// Value selector sub-state (CH/CC choices)
    ValueSelectorState selector;

    /// Macro selector sub-state (macro target while editing)
    MacroSelectorState macroSelector;

    /// Focused row in the automation lifecycle overlay.
    oc::state::Signal<uint8_t, 4> automationFocusedRow{0};

    /// Runtime decision state for long-press open release policy
    uint8_t openedByMacroIndex = 0;
    uint32_t openedAtMs = 0;
    bool pendingOpenReleaseDecision = false;

    ~MacroEditState();

    /**
     * @brief Reset to defaults
     */
    void reset();

    /**
     * @brief Start editing a macro
     *
     * Loads current config into the overlay edit fields and shows overlay.
     *
     * @param index Macro index (0-7)
     * @param channel Current MIDI channel (0-15)
     * @param cc Current CC number (0-127)
     */
    void openEditor(uint8_t index, uint8_t channel, uint8_t cc, uint32_t openedAt);

    void closeEditor();

    void openValueSelector(uint8_t row, int selectedIndex);

    void closeValueSelector();

    void openPageSelector();

    void closePageSelector();

    void openTargetSelector(int selectedIndex);

    void closeTargetSelector();

    void openAutomation();

    void closeAutomation();

    void loadActiveConfig(uint8_t index, uint8_t channel, uint8_t cc);

    bool consumeOpeningReleaseDecision(uint8_t macroIndex,
                                       uint32_t nowMs,
                                       uint32_t quickReleaseWindowMs);
};

}  // namespace core::state

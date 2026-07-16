#pragma once

/**
 * @file MacroEditState.hpp
 * @brief State for macro edit overlay
 *
 * Tracks which macro is being edited and the current overlay-local edit values.
 */

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/macro/MacroAutomationState.hpp"
#include "state/contextual/ContextualActionModels.hpp"

namespace core::state {

enum class MacroEditFlowPhase : uint8_t {
    CLOSED = 0,
    EDIT = 1,
    VALUE_SELECTOR = 2,
    PAGE_SELECTOR = 3,
    TARGET_SELECTOR = 4,
    AUTOMATION = 5,
    MODULATION = 6,
    CONVERT_PREVIEW = 7,
    LFO_AUDITION = 8,
    MODULATOR_PICKER = 9,
    EXISTING_MODULATOR_AUDITION = 10,
    MODULATOR_CREATE = 11,
};

enum class MacroSlotProperty : uint8_t {
    DESTINATION = 0,
    AUTOMATION,
    MODULATION,
    DEPTH,
};

enum class MacroContextButton : uint8_t {
    NONE = 0,
    BOTTOM_LEFT,
    BOTTOM_RIGHT,
};

/**
 * @brief State for macro edit overlay
 *
 * Tracks which macro is being edited and the current overlay-local edit values.
 *
 * The overlay keeps its own editable CC field and the inherited track channel
 * snapshot so the UI can move between rows, selectors, pages, and target macros
 * without repeatedly re-reading state. Only the CC is committed by this editor.
 */
struct MacroEditState {
    /// Overlay visibility (owned by ExclusiveVisibilityStack)
    oc::state::Signal<bool> visible{false};
    oc::state::Signal<bool, 4> automationVisible{false};
    oc::state::Signal<MacroEditFlowPhase, 4> flowPhase{MacroEditFlowPhase::CLOSED};

    /// Which macro is being edited (0-7)
    oc::state::Signal<uint8_t> editingIndex{0};

    /// Current inherited track channel snapshot (0-15)
    oc::state::Signal<uint8_t> tempChannel{0};

    /// Current editable CC value shown by the overlay (0-127)
    oc::state::Signal<uint8_t> tempCC{0};

    /// Focused Slot property (Destination, Automation, Modulation, Depth).
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
    /// Focused row in the modulation lifecycle overlay.
    oc::state::Signal<uint8_t, 4> modulationFocusedRow{0};

    struct ConversionPreviewState {
        core::state::macro::MacroAutomationConversionPolicy policy =
            core::state::macro::MacroAutomationConversionPolicy::MEAN;
        core::state::macro::MacroAutomationConversionPlan plan{};
        oc::state::Signal<uint32_t, 4> revision{0};

        void reset();
        void setPlan(
            const core::state::macro::MacroAutomationConversionPlan& next
        );
    } conversionPreview;

    /** Shared guarded-action lifecycle for the contextual bottom strip. */
    oc::state::Signal<core::state::contextual::GuardedActionState, 6>
        contextGuard{};
    oc::state::Signal<core::state::contextual::OperationFeedbackState, 6>
        contextFeedback{};
    oc::state::Signal<MacroContextButton, 6> contextButton{
        MacroContextButton::NONE
    };

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

    void openModulation(uint8_t focusedRow = 0);

    void closeModulation();

    void openModulatorCreate();

    void closeModulatorCreate(uint8_t focusedRow);

    void openLfoAudition();

    void cancelLfoAudition(uint8_t focusedRow = 0);

    void openModulatorPicker(int selectedIndex = 0);

    void closeModulatorPicker(uint8_t focusedRow = 1);

    void openExistingModulatorAudition();

    void cancelExistingModulatorAudition();

    void applyModulatorAudition();

    void openConvertPreview(
        const core::state::macro::MacroAutomationConversionPlan& plan
    );

    void closeConvertPreview();

    void loadActiveConfig(uint8_t index, uint8_t channel, uint8_t cc);

    bool consumeOpeningReleaseDecision(uint8_t macroIndex,
                                       uint32_t nowMs,
                                       uint32_t quickReleaseWindowMs);
};

}  // namespace core::state

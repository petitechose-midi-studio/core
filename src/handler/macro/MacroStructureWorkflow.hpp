#pragma once

#include <cstdint>

#include <oc/state/FixedSubscriptionList.hpp>
#include <oc/state/Signal.hpp>

#include "handler/macro/MacroStructureDomainServices.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/StructureSelectionInteractionPolicy.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroInteractionContextBuilder.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::handler {

/**
 * Owns macro page/track structure navigation and edit modes.
 *
 * The workflow manages preview, hold, copy/paste, and direct delete intent;
 * domain mutations are delegated to MacroStructureDomainServices.
 */
class MacroStructureWorkflow {
public:
    struct StateRefs {
        core::state::macro::MacroUiState& macroUi;
        core::state::macro::MacroPagesState& pages;
        core::state::TrackNavigationState& trackNavigation;
        oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        oc::state::Signal<
            core::state::StructureNavigationFocus,
            core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
        core::state::StructureClipboardState& structureClipboard;
    };

    MacroStructureWorkflow(StateRefs state, MacroStructureDomainServices services);

    MacroStructureWorkflow(const MacroStructureWorkflow&) = delete;
    MacroStructureWorkflow& operator=(const MacroStructureWorkflow&) = delete;

    core::state::macro::MacroInteractionContext interactionContext(
        bool blockingOverlay,
        bool slotPropertySelecting
    ) const;

    bool commitPreviewedPageIfNeeded();
    void cycleNavigationFocus();
    void setNavigationFocus(core::state::StructureNavigationFocus focus);
    void moveByFocus(float delta);
    bool canRemoveCurrentStructure() const;
    bool canPasteCurrentStructure() const;
    /** Captures every accepted press; armVisualHold selects long-action UI. */
    [[nodiscard]] bool beginHoldAction(
        core::state::StructureHoldAction action,
        bool armVisualHold
    );
    [[nodiscard]] bool hasCapturedAction(
        core::state::StructureHoldAction action
    ) const;
    [[nodiscard]] bool commitHoldAction(core::state::StructureHoldAction action);
    /**
     * Settles an early release and returns true only when an owned captured
     * press still owns its acquisition and exact current structure. Presses
     * without a visual long-action hold retain the same target provenance.
     */
    [[nodiscard]] bool releaseShortHoldAction(
        core::state::StructureHoldAction action
    );
    void applyCurrentStructureShortPress();
    void copyCurrentStructure();
    void createPreviewedStructure();

    void enterSelectionModeForCurrentFocus();
    /** Handles one local Back tier; returns true when a selection owned it. */
    bool backSelectionMode();
    [[nodiscard]] bool selectionActive() const;
    [[nodiscard]] core::state::StructureSelectionInteractionPolicy
        selectionInteractionPolicy() const;
    void navigateSelection(float delta);
    void toggleSelectionAtCursor();
    [[nodiscard]] bool copySelection();
    [[nodiscard]] bool canPasteSelection() const;
    [[nodiscard]] bool pasteSelection();
    [[nodiscard]] uint8_t selectionCursor() const;
    [[nodiscard]] uint32_t selectionClipboardRevision() const;

    [[nodiscard]] bool slotSelectionActive() const;
    void toggleSlotSelectionAtPageIndex(uint8_t macroIndex);

private:
    void applyCurrentStructureLongPress();
    void pasteCurrentStructure();
    [[nodiscard]] bool selectionPlacementActive() const;
    [[nodiscard]] bool selectionHasItems() const;
    void enterSlotSelection();
    [[nodiscard]] bool slotSelectionPlacementActive() const;
    void navigateSlotSelection(float delta);
    void toggleSlotSelectionAtCursor();
    [[nodiscard]] bool copySlotSelection();
    [[nodiscard]] bool canPasteSlotSelection() const;
    [[nodiscard]] bool pasteSlotSelection();
    void refreshSlotSelectionPastePreview();
    void bindStateSync();
    core::state::StructureNavigationFocus effectiveFocus() const;
    void movePage(float delta);
    void moveTrack(float delta);
    void moveMacroSlot(float delta);
    core::state::macro::MacroInteractionContextSource interactionContextSource(
        bool blockingOverlay = false,
        bool slotPropertySelecting = false
    ) const;
    void syncPreviewToCurrentContext();
    void clampFocusedMacroSlot();
    [[nodiscard]] uint8_t existingMacroPageCount() const;
    void enterPageSelection();
    void enterTrackSelection();
    void navigatePageSelection(float delta);
    void navigateTrackSelection(float delta);
    void togglePageSelectionAtCursor();
    void toggleTrackSelectionAtCursor();
    void syncPageSelectionCursorPresentation();
    [[nodiscard]] uint8_t slotSelectionNavigationPageCount() const;
    void syncSlotSelectionCursorPresentation();
    void cancelSlotSelection();
    void clearHoldAction();
    void captureHoldTarget(core::state::StructureHoldAction action);
    [[nodiscard]] bool capturedTargetStillMatches() const;
    [[nodiscard]] bool settleCapturedHoldAction(
        core::state::StructureHoldAction action,
        bool requireVisualHold
    );

    struct HoldTarget {
        static constexpr uint8_t ACTION_MASK = 0x03U;
        static constexpr uint8_t FOCUS_MASK = 0x0CU;
        static constexpr uint8_t FOCUS_SHIFT = 2U;
        static constexpr uint8_t ADD_SLOT = 0x10U;
        static constexpr uint8_t VISUAL_HOLD = 0x20U;

        uint32_t acquisitionId = 0U;
        uint8_t track = 0xFFU;
        uint8_t page = 0xFFU;
        uint8_t macro = 0xFFU;
        uint8_t flags = 0U;

        [[nodiscard]] core::state::StructureHoldAction action() const {
            return static_cast<core::state::StructureHoldAction>(
                flags & ACTION_MASK
            );
        }
        void setAction(core::state::StructureHoldAction value) {
            flags = static_cast<uint8_t>(
                (flags & static_cast<uint8_t>(~ACTION_MASK)) |
                (static_cast<uint8_t>(value) & ACTION_MASK)
            );
        }
        [[nodiscard]] core::state::StructureNavigationFocus focus() const {
            return static_cast<core::state::StructureNavigationFocus>(
                (flags & FOCUS_MASK) >> FOCUS_SHIFT
            );
        }
        void setFocus(core::state::StructureNavigationFocus value) {
            flags = static_cast<uint8_t>(
                (flags & static_cast<uint8_t>(~FOCUS_MASK)) |
                ((static_cast<uint8_t>(value) << FOCUS_SHIFT) & FOCUS_MASK)
            );
        }
        [[nodiscard]] bool addSlot() const {
            return (flags & ADD_SLOT) != 0U;
        }
        void setAddSlot(bool value) {
            flags = value
                ? static_cast<uint8_t>(flags | ADD_SLOT)
                : static_cast<uint8_t>(flags & static_cast<uint8_t>(~ADD_SLOT));
        }
        [[nodiscard]] bool visualHold() const {
            return (flags & VISUAL_HOLD) != 0U;
        }
        void setVisualHold(bool value) {
            flags = value
                ? static_cast<uint8_t>(flags | VISUAL_HOLD)
                : static_cast<uint8_t>(
                    flags & static_cast<uint8_t>(~VISUAL_HOLD)
                );
        }
    };
    static_assert(sizeof(HoldTarget) == 8U);
    static_assert(
        static_cast<uint8_t>(core::state::StructureHoldAction::COUNT) - 1U <=
        HoldTarget::ACTION_MASK
    );
    static_assert(
        ((static_cast<uint8_t>(core::state::StructureNavigationFocus::COUNT) -
          1U) <<
         HoldTarget::FOCUS_SHIFT) <= HoldTarget::FOCUS_MASK
    );

    core::state::macro::MacroUiState& macro_ui_;
    core::state::macro::MacroPagesState& pages_;
    core::state::TrackNavigationState& track_ui_;
    oc::state::Signal<uint8_t, 8>& shared_track_active_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::StructureClipboardState& structure_clipboard_;
    MacroStructureDomainServices services_;
    HoldTarget hold_target_{};
    oc::state::FixedSubscriptionList<2> subscriptions_;
};

static_assert(
    sizeof(void*) != 4U || sizeof(MacroStructureWorkflow) == 120U,
    "Macro Structure workflow must remain in its 120-byte ARM PSRAM envelope"
);

}  // namespace core::handler

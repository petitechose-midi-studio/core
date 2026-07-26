#pragma once

#include <cstdint>

#include <oc/state/FixedSubscriptionList.hpp>
#include <oc/state/Signal.hpp>

#include "handler/macro/MacroStructureDomainServices.hpp"
#include "state/StructureClipboardState.hpp"
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
    void beginHoldAction(core::state::StructureHoldAction action);
    [[nodiscard]] bool hasHoldAction(core::state::StructureHoldAction action) const;
    [[nodiscard]] bool commitHoldAction(core::state::StructureHoldAction action);
    void clearHoldAction();
    void eraseCurrentStructure();
    void removeCurrentStructure();
    void copyCurrentStructure();
    void pasteCurrentStructure();
    void createPreviewedStructure();

private:
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
    void captureHoldTarget(core::state::StructureHoldAction action);
    [[nodiscard]] bool holdTargetStillMatches(
        core::state::StructureHoldAction action
    ) const;

    struct HoldTarget {
        core::state::StructureHoldAction action = core::state::StructureHoldAction::NONE;
        core::state::StructureNavigationFocus focus =
            core::state::StructureNavigationFocus::PAGE;
        uint8_t track = 0xFFU;
        uint8_t page = 0xFFU;
        uint8_t macro = 0xFFU;
        bool addSlot = false;
    };

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

}  // namespace core::handler

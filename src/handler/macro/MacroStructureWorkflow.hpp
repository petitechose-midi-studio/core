#pragma once

#include <vector>

#include <oc/state/Signal.hpp>

#include "handler/macro/MacroStructureDomainServices.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::handler {

/**
 * Owns macro page/track structure navigation and edit modes.
 *
 * The workflow manages preview, selection, hold, copy/paste, delete, and
 * duplicate intent; domain mutations are delegated to MacroStructureDomainServices.
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

    bool selectionActive() const;
    bool previewingAddSlot() const;

    bool commitPreviewedPageIfNeeded();
    void cycleNavigationFocus();
    void moveByFocus(float delta);
    void enterSelectionModeForCurrentFocus();
    void cancelSelectionMode();
    void toggleSelectionAtCursor();
    void navigateSelection(float delta);

    bool canRemoveCurrentStructure() const;
    bool canPasteCurrentStructure() const;
    void beginHoldAction(core::state::StructureHoldAction action);
    void clearHoldAction();
    void eraseCurrentStructure();
    void removeCurrentStructure();
    void copyCurrentStructure();
    void pasteCurrentStructure();
    void deleteSelection();
    void duplicateSelection();
    void createPreviewedStructure();

private:
    void bindStateSync();
    void movePage(float delta);
    void moveTrack(float delta);
    void syncPreviewToCurrentContext();

    core::state::macro::MacroUiState& macro_ui_;
    core::state::macro::MacroPagesState& pages_;
    core::state::TrackNavigationState& track_ui_;
    oc::state::Signal<uint8_t, 8>& shared_track_active_;
    oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigation_focus_;
    core::state::StructureClipboardState& structure_clipboard_;
    MacroStructureDomainServices services_;
    std::vector<oc::state::Subscription> subscriptions_;
};

}  // namespace core::handler

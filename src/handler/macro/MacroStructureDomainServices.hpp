#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

/**
 * Macro structure domain service boundary.
 *
 * It applies page/track mask changes, duplication, paste, erase, active
 * selection, project mutation, and presentation refresh through focused state
 * refs and typed operations.
 */
class MacroStructureDomainServices {
public:
    using FlushMutationCoalescingFn = void (*)(void* context);
    using MarkProjectMutatedFn = void (*)(void* context);
    using SetSharedTrackStateFn = bool (*)(void* context, uint16_t enabledMask, uint8_t activeTrack);
    using SwitchToPageFn = void (*)(void* context, uint8_t pageIndex);
    using SwitchToTrackFn = void (*)(void* context, uint8_t trackIndex);

    struct StateRefs {
        core::state::MacroState& macros;
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
        oc::state::Signal<uint32_t>& configRevision;
        core::state::StatusBarState& statusBar;
        oc::state::Signal<uint8_t, 8>& sharedTrackActive;
        oc::state::Signal<uint16_t, 16>& sharedTrackEnabledMask;
    };

    struct Operations {
        void* context = nullptr;
        FlushMutationCoalescingFn flushMutationCoalescing = nullptr;
        MarkProjectMutatedFn markProjectMutated = nullptr;
        SetSharedTrackStateFn setSharedTrackState = nullptr;
        SwitchToPageFn switchToPage = nullptr;
        SwitchToTrackFn switchToTrack = nullptr;
    };

    MacroStructureDomainServices(StateRefs state, Operations operations);
    static MacroStructureDomainServices fromCoreState(core::state::CoreState& state);

    void switchToPage(uint8_t pageIndex) const;
    void switchToTrack(uint8_t trackIndex) const;
    uint8_t activeTrack() const;
    uint16_t pageEnabledMask() const;
    uint16_t trackEnabledMask() const;
    bool deleteActivePage() const;
    bool deleteActiveTrack() const;
    bool deleteSelectedPages(uint16_t selectedMask) const;
    bool deleteSelectedTracks(uint16_t selectedMask) const;
    bool duplicateSelectedPages(uint16_t selectedMask) const;
    bool duplicateSelectedTracks(uint16_t selectedMask) const;
    bool erasePage(uint8_t pageIndex) const;
    bool eraseTrack(uint8_t trackIndex) const;
    bool pastePage(uint8_t pageIndex,
                   const core::state::macro::MacroPageData& pageData,
                   const core::state::MacroAutomationClipboard* automation = nullptr) const;
    bool pasteTrack(uint8_t trackIndex,
                    const core::state::macro::MacroTrackData& trackData,
                    const core::state::MacroAutomationClipboard* automation = nullptr) const;
    bool createNextPage() const;
    bool createTrack(uint8_t trackIndex) const;
    bool activateMacroSlot(uint8_t index) const;
    bool macroAutomationActive(uint8_t index) const;
    bool clearMacroAutomation(uint8_t index) const;
    bool removeMacroAutomation(uint8_t index) const;
    bool copyMacroAutomation(uint8_t index, core::state::StructureClipboardState& clipboard) const;
    bool pasteMacroAutomation(uint8_t index, const core::state::StructureClipboardState& clipboard) const;

private:
    StateRefs stateRefs_() const;

    core::state::MacroState* macros_ = nullptr;
    core::state::macro::MacroPagesState* pages_ = nullptr;
    core::state::macro::MacroUiState* macro_ui_ = nullptr;
    oc::state::Signal<uint32_t>* config_revision_ = nullptr;
    core::state::StatusBarState* status_bar_ = nullptr;
    oc::state::Signal<uint8_t, 8>* shared_track_active_ = nullptr;
    oc::state::Signal<uint16_t, 16>* shared_track_enabled_mask_ = nullptr;
    Operations operations_{};
};

}  // namespace core::handler

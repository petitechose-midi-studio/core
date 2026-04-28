#pragma once

/**
 * @file CoreState.hpp
 * @brief Global state aggregate for standalone mode
 *
 * CoreState lives at application level (in main.cpp) and survives
 * context switches. This allows state persistence across context
 * activate/deactivate cycles.
 *
 * Includes:
 * - MacroState: Runtime values and labels for 8 macro slots
 * - MacroPagesState: macro track/page configurations
 * - CoreSettings: persistence manager for core settings storage
 * - ExclusiveVisibilityStack: Overlay visibility management
 */

#include <cstdint>
#include <memory>

#include <oc/interface/IStorage.hpp>
#include <oc/state/AutoPersistIncremental.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "CoreSettings.hpp"
#include "DataManagerState.hpp"
#include "GlobalSettingsState.hpp"
#include "MidiSyncState.hpp"
#include "MacroEditState.hpp"
#include "MacroState.hpp"
#include "StructureClipboardState.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "StatusBarState.hpp"
#include "TrackNavigationState.hpp"
#include "ViewSelectorState.hpp"
#include "persistence/MacroPersistence.hpp"
#include "persistence/SequencerPersistence.hpp"
#include "macro/MacroPagesState.hpp"
#include "macro/MacroUiState.hpp"
#include "sequencer/SequencerState.hpp"
#include "sequencer/SequencerSnapshots.hpp"
#include "sequencer/SequencerTrackBankState.hpp"

namespace core::state {

struct CoreStateBootstrap;
struct CoreStateLifecycle;

/**
 * Owns the macro runtime, page bank, persistence adapters, and delayed save state.
 *
 * CoreState exposes references to these members for existing call sites, but this
 * domain struct remains the ownership boundary for allocation and persistence.
 */
struct MacroDomainState {
    static constexpr uint32_t WORKSPACE_MUTATION_SAVE_DELAY_MS = 1000;

    core::app::ExtmemUniquePtr<MacroState> runtime;
    core::app::ExtmemUniquePtr<macro::MacroPagesState> pages;
    oc::state::Signal<uint32_t> configRevision{0};
    persistence::MacroPersistence persistence;
    bool persistenceReady = false;
    std::unique_ptr<oc::state::AutoPersistIncremental<MACRO_COUNT>> autoPersist;
    bool workspacePersistPending = false;
    uint32_t workspacePersistTimestampMs = 0;
    uint32_t lastInteractionTimestampMs = 0;

    MacroDomainState(oc::interface::IStorage& workspaceStorage,
                     oc::interface::IStorage& libraryStorage)
        : runtime(core::app::makeExtmemUnique<MacroState>())
        , pages(core::app::makeExtmemUnique<macro::MacroPagesState>())
        , persistence(workspaceStorage, libraryStorage) {}

    MacroDomainState(const MacroDomainState&) = delete;
    MacroDomainState& operator=(const MacroDomainState&) = delete;
};

/**
 * Owns the editable sequencer state, per-track bank, persistence adapters, and
 * pending load snapshots staged while transport is playing.
 */
struct SequencerDomainState {
    struct PendingApply {
        bool valid = false;
        int16_t anchorPlayhead = -1;
        bool merge = false;
        bool fullBank = false;
        sequencer::SequencerPatternSnapshot snapshot{};
        sequencer::SequencerTrackBankSnapshot bankSnapshot{};
    };

    struct PendingApplyDeleter {
        void operator()(PendingApply* ptr) const noexcept;
    };

    using PendingApplyPtr = std::unique_ptr<PendingApply, PendingApplyDeleter>;

    core::app::ExtmemUniquePtr<sequencer::SequencerState> editor;
    core::app::ExtmemUniquePtr<sequencer::SequencerTrackBankState> tracks;
    persistence::SequencerPersistence persistence;
    bool persistenceReady = false;
    PendingApplyPtr pendingApply;
    std::unique_ptr<oc::state::AutoPersistIncremental<10>> autoPersist;

    SequencerDomainState(oc::interface::IStorage& workspaceStorage,
                         oc::interface::IStorage& patternLibraryStorage,
                         oc::interface::IStorage& setLibraryStorage);

    SequencerDomainState(const SequencerDomainState&) = delete;
    SequencerDomainState& operator=(const SequencerDomainState&) = delete;
};

/**
 * Owns UI/session-only state shared by views and overlays.
 *
 * This domain is reset on standalone transient resets; durable macro/sequencer
 * data stays in the dedicated domain structs above.
 */
struct UiSystemState {
    struct SharedTrackState {
        oc::state::Signal<uint8_t, 8> activeIndex{0};
        oc::state::Signal<uint16_t, 16> enabledMask{0x0001};
    };

    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType> overlays;
    oc::state::Signal<core::ui::ViewType, 8> activeView{core::ui::ViewType::MACRO};
    oc::state::Signal<core::state::StructureNavigationFocus, kStructureNavigationFocusMaxSubscribers>
        structureNavigationFocus{
        core::state::StructureNavigationFocus::PAGE
    };
    SharedTrackState sharedTracks;
    TrackNavigationState trackNavigation;
    StructureClipboardState structureClipboard;
    ViewSelectorState viewSelector;
    StatusBarState statusBar;
    MidiSyncState midiSync;
    GlobalSettingsState globalSettings;
    DataManagerState dataManager;
    MacroEditState macroEdit;
    macro::MacroUiState macroUi;

    UiSystemState();
};

/**
 * @brief Global state container for standalone mode.
 *
 * CoreState is the application-level owner for durable macro/sequencer domains,
 * shared UI state, and the small settings store. Contexts receive references to
 * it so activation changes do not erase persisted runtime intent.
 */
struct CoreState {
    friend struct CoreStateBootstrap;
    friend struct CoreStateLifecycle;

private:
    MacroDomainState macroDomain_;
    SequencerDomainState sequencerDomain_;
    core::app::ExtmemUniquePtr<UiSystemState> systemUi_;
    bool sharedTrackPersistPending_ = false;
    uint32_t sharedTrackPersistTimestampMs_ = 0;

public:
    /// Persistence manager
    CoreSettings settings;

    /// Macro domain aliases
    MacroState& macros;
    macro::MacroPagesState& pages;
    oc::state::Signal<uint32_t>& configRevision;
    persistence::MacroPersistence& macroPersistence;

    /// Sequencer domain aliases
    sequencer::SequencerState& sequencer;
    sequencer::SequencerTrackBankState& sequencerTracks;
    persistence::SequencerPersistence& sequencerPersistence;

    /// Shared UI/system domain aliases
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
    oc::state::Signal<core::ui::ViewType, 8>& activeView;
    oc::state::Signal<core::state::StructureNavigationFocus, kStructureNavigationFocusMaxSubscribers>&
        structureNavigationFocus;
    oc::state::Signal<uint8_t, 8>& sharedTrackActive;
    oc::state::Signal<uint16_t, 16>& sharedTrackEnabledMask;
    TrackNavigationState& trackNavigation;
    StructureClipboardState& structureClipboard;
    ViewSelectorState& viewSelector;
    StatusBarState& statusBar;
    MidiSyncState& midiSync;
    GlobalSettingsState& globalSettings;
    DataManagerState& dataManager;
    MacroEditState& macroEdit;
    macro::MacroUiState& macroUi;

    /**
     * @brief Construct with storage backend
     * @param settingsStorage Core settings storage (midi sync + shortcuts)
     * @param macroWorkspaceStorage Dedicated macro workspace storage
     * @param macroLibraryStorage Dedicated macro library storage
     * @param sequencerWorkspaceStorage Dedicated sequencer workspace storage
     * @param sequencerPatternLibraryStorage Dedicated sequencer pattern library storage
     * @param sequencerSetLibraryStorage Dedicated sequencer set library storage
     */
    explicit CoreState(oc::interface::IStorage& settingsStorage,
                       oc::interface::IStorage& macroWorkspaceStorage,
                       oc::interface::IStorage& macroLibraryStorage,
                       oc::interface::IStorage& sequencerWorkspaceStorage,
                       oc::interface::IStorage& sequencerPatternLibraryStorage,
                       oc::interface::IStorage& sequencerSetLibraryStorage);

    // Non-copyable, non-movable
    CoreState(const CoreState&) = delete;
    CoreState& operator=(const CoreState&) = delete;
    CoreState(CoreState&&) = delete;
    CoreState& operator=(CoreState&&) = delete;

    // Persistence and runtime coordination.

    /**
     * @brief Update persistence (call from main loop)
     *
     * Saves dirty values incrementally after debounce timeout.
     */
    void update();

    /**
     * @brief Factory reset - clear all settings
     */
    void factoryReset();

    /**
     * @brief Flush any pending dirty values immediately
     */
    void flush();
    void flushAutoPersist();
    void resetStandaloneTransientUi();

    bool isMacroPersistenceReady() const;
    bool isSequencerPersistenceReady() const;
    void requestMacroWorkspacePersist();
    void persistSequencerWorkspace();
    void queuePendingSequencerApply(const sequencer::SequencerState& staged, bool merge = false);
    void queuePendingSequencerBankApply(const sequencer::SequencerTrackBankSnapshot& staged);
    void clearPendingSequencerApply();
    bool hasPendingSequencerApply() const;
    uint16_t currentSharedTrackEnabledMask() const;
    uint8_t currentSharedActiveTrack() const;
    bool setSharedTrackState(uint16_t enabledMask, uint8_t activeTrack);
    bool refreshSharedTrackStateFromMacroPages();
    bool refreshSharedTrackStateFromSequencer();
    void noteMacroInteraction();

private:
    void queueSequencerApply_(const sequencer::SequencerState& staged, bool merge = false);
    void queueSequencerBankApply_(const sequencer::SequencerTrackBankSnapshot& staged);
    void requestMacroWorkspacePersist_();
    void requestSharedTrackPersist_();
    void persistMacroWorkspaceNow_();
    void persistSequencerWorkspace_();
    void persistSharedTrackState_();
    void clearPendingSequencerApply_();
    bool refreshSharedTrackStateFromMacroPages_(bool persist);
    bool refreshSharedTrackStateFromSequencer_(bool persist);
    bool setSharedTrackState_(uint16_t enabledMask, uint8_t activeTrack, bool persist);

};

}  // namespace core::state

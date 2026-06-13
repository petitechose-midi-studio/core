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

#include <array>
#include <cstdint>
#include <memory>

#include <oc/interface/IStorage.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/state/AutoPersistIncremental.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "CoreSettings.hpp"
#include "DataManagerState.hpp"
#include "DeviceSettingsState.hpp"
#include "PatternPitchSettingsState.hpp"
#include "SequencerSettingsState.hpp"
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
#include "sequencer/SequencerHistory.hpp"
#include "sequencer/SequencerSnapshots.hpp"
#include "sequencer/SequencerTrackBankState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectState.hpp"

namespace core::state {

struct CoreStateBootstrap;
struct CoreStateLifecycle;

/**
 * Owns the macro runtime, page bank, persistence adapter, and projection state.
 *
 * CoreState exposes public references to the runtime/page state while this
 * domain struct remains the ownership boundary for allocation and persistence.
 */
struct MacroDomainState {
    core::app::ExtmemUniquePtr<MacroState> runtime;
    core::app::ExtmemUniquePtr<macro::MacroPagesState> pages;
    oc::state::Signal<uint32_t> configRevision{0};
    persistence::MacroPersistence persistence;
    bool persistenceReady = false;
    std::unique_ptr<oc::state::AutoPersistIncremental<MACRO_COUNT>> autoPersist;

    explicit MacroDomainState(oc::interface::IStorage& libraryStorage)
        : runtime(core::app::makeExtmemUnique<MacroState>())
        , pages(core::app::makeExtmemUnique<macro::MacroPagesState>())
        , persistence(libraryStorage) {}
    ~MacroDomainState();

    MacroDomainState(const MacroDomainState&) = delete;
    MacroDomainState& operator=(const MacroDomainState&) = delete;
};

/**
 * Owns the editable sequencer state, per-track bank, persistence adapter, and
 * pending load snapshots staged while transport is playing.
 */
struct SequencerDomainState {
    static constexpr uint32_t COALESCED_PATTERN_HISTORY_IDLE_MS = 500;

    struct PendingApply {
        bool valid = false;
        int16_t anchorPlayhead = -1;
        bool merge = false;
        bool fullBank = false;
        sequencer::SequencerPatternSnapshot snapshot{};
        sequencer::SequencerTrackBankSnapshot bankSnapshot{};
        core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> patternGraph;
        std::array<
            core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph>,
            sequencer::SequencerTrackBankState::TRACK_COUNT
        > bankGraphs{};
    };

    struct PendingApplyDeleter {
        void operator()(PendingApply* ptr) const noexcept;
    };

    using PendingApplyPtr = std::unique_ptr<PendingApply, PendingApplyDeleter>;

    struct CoalescedPatternHistory {
        bool pending = false;
        uint8_t activeTrack = 0;
        uint8_t step = 0;
        sequencer::StepProperty property = sequencer::StepProperty::NOTE;
        uint32_t lastTouchedMs = 0;
        sequencer::SequencerHistoryPatternSnapshot before{};

        bool matches(uint8_t nextActiveTrack,
                     uint8_t nextStep,
                     sequencer::StepProperty nextProperty) const {
            return pending &&
                   activeTrack == nextActiveTrack &&
                   step == nextStep &&
                   property == nextProperty;
        }

        void clear() {
            pending = false;
            activeTrack = 0;
            step = 0;
            property = sequencer::StepProperty::NOTE;
            lastTouchedMs = 0;
            before = sequencer::SequencerHistoryPatternSnapshot{};
        }
    };

    core::app::ExtmemUniquePtr<sequencer::SequencerState> editor;
    core::app::ExtmemUniquePtr<sequencer::SequencerTrackBankState> tracks;
    sequencer::SequencerHistoryService history;
    persistence::SequencerPersistence persistence;
    bool persistenceReady = false;
    PendingApplyPtr pendingApply;
    CoalescedPatternHistory coalescedPatternHistory;
    std::unique_ptr<oc::state::AutoPersistIncremental<13>> autoPersist;

    SequencerDomainState(oc::interface::IStorage& patternLibraryStorage,
                         oc::interface::IStorage& setLibraryStorage);
    ~SequencerDomainState();

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
    DeviceSettingsState deviceSettings;
    SequencerSettingsState sequencerSettings;
    PatternPitchSettingsState patternPitchSettings;
    DataManagerState dataManager;
    MacroEditState macroEdit;
    macro::MacroUiState macroUi;
    project::ProjectNavigationState projectNavigation;

    UiSystemState();
    ~UiSystemState();
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

public:
    static constexpr uint32_t PROJECT_SESSION_AUTOSAVE_DELAY_MS = 2000;

private:
    MacroDomainState macroDomain_;
    SequencerDomainState sequencerDomain_;
    project::ProjectState project_;
    core::app::ExtmemUniquePtr<UiSystemState> systemUi_;
    bool projectSessionTrackingEnabled_ = false;
    bool projectSessionSavePending_ = false;
    uint32_t projectSessionSaveTimestampMs_ = 0;
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
    sequencer::SequencerHistoryService& sequencerHistory;
    persistence::SequencerPersistence& sequencerPersistence;

    /// Shared UI/system domain aliases
    project::ProjectState& project;
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
    DeviceSettingsState& deviceSettings;
    SequencerSettingsState& sequencerSettings;
    PatternPitchSettingsState& patternPitchSettings;
    DataManagerState& dataManager;
    MacroEditState& macroEdit;
    macro::MacroUiState& macroUi;
    project::ProjectNavigationState& projectNavigation;

    /**
     * @brief Construct with storage backend
     * @param settingsStorage Core settings storage (midi sync + shortcuts)
     * @param macroLibraryStorage Dedicated macro library storage
     * @param sequencerPatternLibraryStorage Dedicated sequencer pattern library storage
     * @param sequencerSetLibraryStorage Dedicated sequencer set library storage
     */
    explicit CoreState(oc::interface::IStorage& settingsStorage,
                       oc::interface::IStorage& macroLibraryStorage,
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
    void resetMusicalProject();
    void markProjectMutated();
    void requestProjectSessionSave();
    void acknowledgeProjectSessionSave(uint32_t savedModifiedCounter);
    bool hasPendingProjectSessionSave() const;
    uint32_t projectSessionSaveTimestampMs() const;

    bool isMacroPersistenceReady() const;
    bool isSequencerPersistenceReady() const;
    void markSequencerProjectMutated();
    bool recordSequencerPatternHistory(sequencer::SequencerHistoryPatternSnapshot before,
                                       sequencer::SequencerHistoryPatternSnapshot after,
                                       sequencer::SequencerHistoryDescriptor descriptor = {});
    bool recordSequencerBankHistory(sequencer::SequencerHistoryTrackBankSnapshot before,
                                    sequencer::SequencerHistoryTrackBankSnapshot after,
                                    sequencer::SequencerHistoryDescriptor descriptor = {});
    bool recordSequencerBankHistory(sequencer::SequencerHistoryFullBankChangePtr change);
    bool recordSequencerStructureHistory(sequencer::SequencerHistoryTrackStructureChangePtr change);
    bool beginOrContinueSequencerPatternHistoryCoalescing(uint8_t step,
                                                          sequencer::StepProperty property,
                                                          uint32_t nowMs);
    bool commitSequencerPatternHistoryCoalescing();
    bool updateSequencerPatternHistoryCoalescing(uint32_t nowMs);
    bool hasPendingSequencerPatternHistoryCoalescing() const;
    bool undoSequencerHistory();
    bool redoSequencerHistory();
    void clearSequencerHistory();
    void queuePendingSequencerApply(const sequencer::SequencerState& staged, bool merge = false);
    void queuePendingSequencerBankApply(const sequencer::SequencerTrackBankState& stagedBank,
                                        const sequencer::SequencerState& staged);
    void clearPendingSequencerApply();
    bool hasPendingSequencerApply() const;
    uint16_t currentSharedTrackEnabledMask() const;
    uint8_t currentSharedActiveTrack() const;
    bool setSharedTrackState(uint16_t enabledMask, uint8_t activeTrack);
    bool refreshSharedTrackStateFromMacroPages();
    bool refreshSharedTrackStateFromSequencer();

    /**
     * @brief Reinitialize explicit persistence domains and save current RAM to storage.
     *
     * Used after platform code has reopened storage. Retired macro/sequencer
     * domain stores are not part of session recovery.
     */
    persistence::PersistenceWriteStatus recoverPersistenceFromRamAfterStorageReopen();

private:
    void queueSequencerApply_(const sequencer::SequencerState& staged, bool merge = false);
    void queueSequencerBankApply_(const sequencer::SequencerTrackBankState& stagedBank,
                                  const sequencer::SequencerState& staged);
    void requestProjectSessionSave_();
    void markSequencerProjectMutated_();
    void requestSharedTrackPersist_();
    void persistSharedTrackState_();
    void clearPendingSequencerApply_();
    bool refreshSharedTrackStateFromMacroPages_(bool persist);
    bool refreshSharedTrackStateFromSequencer_(bool persist);
    bool setSharedTrackState_(uint16_t enabledMask, uint8_t activeTrack, bool persist);

};

}  // namespace core::state

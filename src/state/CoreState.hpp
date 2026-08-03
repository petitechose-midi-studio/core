#pragma once

/**
 * @file CoreState.hpp
 * @brief Global state aggregate for standalone mode
 *
 * CoreState lives at application level (in main.cpp) and survives context
 * switches. It is the in-memory authority across context activate/deactivate
 * cycles; ProductFileService owns Project and preset files.
 *
 * Includes:
 * - MacroState: Runtime values and labels for 8 macro slots
 * - MacroPagesState: macro track/page configurations
 * - DeviceSettingsStore: durable controller-settings persistence
 * - ExclusiveVisibilityStack: Overlay visibility management
 */

#include "DeviceSettingsState.hpp"
#include "MacroEditState.hpp"
#include "MacroState.hpp"
#include "MidiSyncState.hpp"
#include "PatternPitchSettingsState.hpp"
#include "SequencerSettingsState.hpp"
#include "StatusBarState.hpp"
#include "StructureClipboardState.hpp"
#include "TrackNavigationState.hpp"
#include "ViewSelectorState.hpp"

#include <cstdint>

#include <array>
#include <memory>

#include <oc/interface/IStorage.hpp>
#include <oc/note/sequencer/StepSequencerGraph.hpp>
#include <oc/state/ChangeCoalescer.hpp>
#include <oc/state/ExclusiveVisibilityStack.hpp>
#include <oc/state/Signal.hpp>

#include "app/ExtmemAllocator.hpp"
#include "app/OverlayTypes.hpp"
#include "app/ViewTypes.hpp"
#include "macro/MacroHistory.hpp"
#include "macro/MacroPagesState.hpp"
#include "macro/MacroUiState.hpp"
#include "persistence/DeviceSettingsStore.hpp"
#include "persistence/PersistenceStatus.hpp"
#include "state/project/ProjectHistoryCoordinator.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/project/ProjectSettingsHistory.hpp"
#include "state/project/ProjectState.hpp"
#include "state/project/ProjectTrackEditorState.hpp"
#include "state/project/ProjectTrackHistory.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::sequencer {
class MidiCcGlobalFrameCoordinator;
}

namespace core::state {

struct CoreStateBootstrap;
struct CoreStateLifecycle;

namespace sequencer {
struct SequencerGraphCompactionRemap;
struct SequencerPreparedGraphContentPath;
}  // namespace sequencer

/**
 * Owns the macro runtime, page bank, history, and projection state.
 *
 * CoreState exposes public references to the runtime/page state while this
 * domain struct remains the in-memory ownership and allocation boundary.
 */
struct MacroDomainState {
    static constexpr uint32_t COALESCED_VALUE_HISTORY_IDLE_MS = 500U;

    struct CoalescedValueHistory {
        bool pending = false;
        macro::MacroAutomationSlotAddress address{};
        uint32_t lastTouchedMs = 0U;

        void clear() {
            pending = false;
            address = {};
            lastTouchedMs = 0U;
        }
    };

    core::app::ExtmemUniquePtr<MacroState> runtime;
    core::app::ExtmemUniquePtr<macro::MacroPagesState> pages;
    macro::MacroHistoryService history;
    oc::state::Signal<uint32_t> runtimeOwnerRevision{1};
    oc::state::Signal<uint32_t> configRevision{0};
    std::unique_ptr<oc::state::ChangeCoalescer<>> mutationCoalescer;
    CoalescedValueHistory coalescedValueHistory{};

    MacroDomainState();
    ~MacroDomainState();

    MacroDomainState(const MacroDomainState&) = delete;
    MacroDomainState& operator=(const MacroDomainState&) = delete;
};

/**
 * Owns the editable sequencer state, per-track bank, history, and pending
 * Project snapshots staged while transport is playing.
 */
struct SequencerDomainState {
    static constexpr uint32_t COALESCED_PATTERN_HISTORY_IDLE_MS = 500;
    static constexpr uint32_t COALESCED_CC_LANE_HISTORY_IDLE_MS = 320;

    struct PendingApply {
        bool valid = false;
        int16_t anchorPlayhead = -1;
        bool merge = false;
        bool fullBank = false;
        sequencer::SequencerPatternSnapshot snapshot{};
        sequencer::SequencerTrackBankSnapshot bankSnapshot{};
        core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> patternGraph;
        core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph> activeTrackGraph;
        core::app::ExtmemUniquePtr<sequencer::SequencerCcLaneBank> patternCcLanes;
        core::app::ExtmemUniquePtr<sequencer::SequencerCcLaneBank> activeTrackCcLanes;
        uint32_t patternCcLaneRevision = 0;
        std::array<core::app::ExtmemUniquePtr<oc::note::sequencer::StepSequencerGraph>,
                   sequencer::SequencerTrackBankState::TRACK_COUNT>
            bankGraphs{};
        std::array<core::app::ExtmemUniquePtr<sequencer::SequencerCcLaneBank>,
                   sequencer::SequencerTrackBankState::TRACK_COUNT>
            bankCcLanes{};
        std::array<uint32_t, sequencer::SequencerTrackBankState::TRACK_COUNT> bankCcLaneRevisions{};
    };

    struct PendingApplyDeleter {
        void operator()(PendingApply* ptr) const noexcept;
    };

    using PendingApplyPtr = std::unique_ptr<PendingApply, PendingApplyDeleter>;

    struct CoalescedPatternHistory {
        enum class Kind : uint8_t {
            StepProperty = 0,
            CcLaneEvent,
            PreparedFamily,
        };

        enum class GraphCompactionState : uint8_t {
            Disabled = 0,
            SealPending,
            PrecompactedUnchanged,
            Precompacted,
        };

        bool pending = false;
        Kind kind = Kind::StepProperty;
        uint8_t activeTrack = 0;
        uint8_t step = 0;
        union {
            sequencer::StepProperty property = sequencer::StepProperty::NOTE;
            sequencer::SequencerPreparedPatternEditOwner familyOwner;
        };
        bool stateProperty = false;
        union {
            uint8_t lane = sequencer::SequencerHistoryDescriptor::INVALID_INDEX;
            uint8_t familyKey;
        };
        uint32_t lastTouchedMs = 0;
        sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan =
            sequencer::SequencerCoalescedPatternPayloadPlan::FlatOnly;
        bool sealed = false;
        bool hasChange = false;
        bool prospectiveGraphInstalled = false;
        bool genericMutationPendingAtBegin = false;
        // Fits existing pointer-alignment padding on both supported ABIs.
        GraphCompactionState graphCompaction = GraphCompactionState::Disabled;
        sequencer::SequencerHistoryPatternChangePtr preparedPatternChange;
        sequencer::SequencerPreparedActiveTrackSynchronization synchronization;
        sequencer::SequencerHistoryPatternChangePtr preparedCcLaneChange;

        bool matchesStepProperty(uint8_t nextActiveTrack, uint8_t nextStep,
                                 sequencer::StepProperty nextProperty,
                                 bool nextStateProperty) const {
            return pending && kind == Kind::StepProperty && activeTrack == nextActiveTrack &&
                   step == nextStep && property == nextProperty &&
                   stateProperty == nextStateProperty;
        }

        bool matchesCcLaneEvent(uint8_t nextActiveTrack, uint8_t nextLane, uint8_t nextStep) const {
            return pending && kind == Kind::CcLaneEvent && activeTrack == nextActiveTrack &&
                   lane == nextLane && step == nextStep;
        }

        bool matchesPreparedFamily(uint8_t nextActiveTrack,
                                   sequencer::SequencerPreparedPatternEditOwner nextOwner,
                                   uint8_t nextKey) const {
            return pending && kind == Kind::PreparedFamily && activeTrack == nextActiveTrack &&
                   familyOwner == nextOwner && familyKey == nextKey;
        }

        bool graphCompactionRequested() const {
            return graphCompaction != GraphCompactionState::Disabled;
        }

        bool graphWasPrecompacted() const {
            return graphCompaction == GraphCompactionState::PrecompactedUnchanged ||
                   graphCompaction == GraphCompactionState::Precompacted;
        }

        void clear();
    };

    core::app::ExtmemUniquePtr<sequencer::SequencerState> editor;
    core::app::ExtmemUniquePtr<sequencer::SequencerTrackBankState> tracks;
    core::app::ExtmemUniquePtr<sequencer::SequencerHistoryService> history;
    sequencer::SequencerTrackActivationQueue trackActivations;
    oc::state::Signal<uint32_t> runtimeProjectRevision{1};
    PendingApplyPtr pendingApply;
    CoalescedPatternHistory coalescedPatternHistory;
    std::unique_ptr<oc::state::ChangeCoalescer<15>> mutationCoalescer;

    SequencerDomainState();
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
    oc::state::Signal<core::state::StructureNavigationFocus,
                      kStructureNavigationFocusMaxSubscribers>
        structureNavigationFocus{core::state::StructureNavigationFocus::PAGE};
    SharedTrackState sharedTracks;
    TrackNavigationState trackNavigation;
    StructureClipboardState structureClipboard;
    ViewSelectorState viewSelector;
    StatusBarState statusBar;
    MidiSyncState midiSync;
    DeviceSettingsState deviceSettings;
    SequencerSettingsState sequencerSettings;
    PatternPitchSettingsState patternPitchSettings;
    MacroEditState macroEdit;
    macro::MacroUiState macroUi;
    project::ProjectNavigationState projectNavigation;
    project::ProjectTrackEditorState projectTrackEditor;

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
    core::app::ExtmemUniquePtr<project::ProjectTrackState> projectTracks_;
    core::app::ExtmemUniquePtr<project::ProjectTrackHistoryService> projectTrackHistory_;
    core::app::ExtmemUniquePtr<project::ProjectSettingsHistoryService> projectSettingsHistory_;
    core::app::ExtmemUniquePtr<project::ProjectHistoryCoordinator> projectHistory_;
    core::app::ExtmemUniquePtr<UiSystemState> systemUi_;
    bool projectSessionTrackingEnabled_ = false;
    bool projectSessionSavePending_ = false;
    uint32_t projectSessionSaveTimestampMs_ = 0;
public:
    /// Durable device settings only; musical content is file-based.
    persistence::DeviceSettingsStore deviceSettingsStore;

    /// Macro domain aliases
    MacroState& macros;
    macro::MacroPagesState& pages;
    macro::MacroHistoryService& macroHistory;
    oc::state::Signal<uint32_t>& macroRuntimeOwnerRevision;
    oc::state::Signal<uint32_t>& configRevision;
    /// Sequencer domain aliases
    sequencer::SequencerState& sequencer;
    sequencer::SequencerTrackBankState& sequencerTracks;
    sequencer::SequencerHistoryService& sequencerHistory;
    sequencer::SequencerTrackActivationQueue& sequencerTrackActivations;
    oc::state::Signal<uint32_t>& sequencerRuntimeProjectRevision;
    // Published by the singular SequencerRuntimeService. Feature modules may
    // produce immutable CC author frames through this non-owning handle, but
    // CoreState never owns or destroys the realtime coordinator.
    core::sequencer::MidiCcGlobalFrameCoordinator* midiCcCoordinator = nullptr;

    /// Shared UI/system domain aliases
    project::ProjectState& project;
    project::ProjectTrackState& projectTracks;
    project::ProjectTrackHistoryService& projectTrackHistory;
    project::ProjectSettingsHistoryService& projectSettingsHistory;
    project::ProjectHistoryCoordinator& projectHistory;
    oc::state::ExclusiveVisibilityStack<core::ui::OverlayType>& overlays;
    oc::state::Signal<core::ui::ViewType, 8>& activeView;
    oc::state::Signal<core::state::StructureNavigationFocus,
                      kStructureNavigationFocusMaxSubscribers>& structureNavigationFocus;
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
    MacroEditState& macroEdit;
    macro::MacroUiState& macroUi;
    project::ProjectNavigationState& projectNavigation;
    project::ProjectTrackEditorState& projectTrackEditor;

    /**
     * @brief Construct with storage backend
     * @param deviceSettingsStorage Durable device-settings storage (MIDI sync)
     */
    explicit CoreState(oc::interface::IStorage& deviceSettingsStorage);
    ~CoreState();

    // Non-copyable, non-movable
    CoreState(const CoreState&) = delete;
    CoreState& operator=(const CoreState&) = delete;
    CoreState(CoreState&&) = delete;
    CoreState& operator=(CoreState&&) = delete;

    // Settings persistence and runtime coordination.

    /**
     * @brief Update runtime coordination
     *
     * Project files and presets are coordinated by ProductFileService.
     */
    void update();

    /**
     * @brief Reset runtime musical state and durable device settings
     *
     * Existing Project and preset files are not deleted.
     */
    void factoryReset();

    /**
     * @brief Flush pending history coalescing
     */
    void flush();
    void flushProjectMutationCoalescing();
    void resetStandaloneTransientUi();
    void resetMusicalProject();
    void requestMacroRuntimeOwnerActivation();
    void requestSequencerRuntimeProjectReset();
    void markMacroValueEdited(uint8_t index);
    [[nodiscard]] bool setMacroValueWithHistory(uint8_t index, float value);
    [[nodiscard]] bool takeMacroManualControlWithHistory(uint8_t index, float value,
                                                         bool coalesceValue);
    [[nodiscard]] bool resumeMacroComputedSourcesWithHistory(uint8_t index);
    void updateMacroValueHistoryCoalescing(uint32_t nowMs);
    void flushMacroValueHistoryCoalescing();
    void markProjectMutated();
    void requestProjectSessionSave();
    void acknowledgeProjectSessionSave(uint32_t savedModifiedCounter);
    bool hasPendingProjectSessionSave() const;
    uint32_t projectSessionSaveTimestampMs() const;
    bool hasPendingProjectMutationCoalescing() const;
    bool hasPendingProjectTransaction() const;

    void markSequencerProjectMutated();
    // Prepared transactions have already synchronized editor and bank. Consume
    // only this coalescer's watched notifications, then publish dirty/save once
    // without cloning a cold payload or draining unrelated callbacks.
    void publishPreparedSequencerMutation(
        bool notifyProjectNavigation = true
    );
    sequencer::SequencerPreparedFullBankEditResult applyPreparedProjectScaleChoice(
        sequencer::SequencerPreparedFullBankEditOwner owner,
        uint8_t row,
        int choiceIndex
    );
    bool canRecordSequencerStructureHistory(
        const sequencer::SequencerHistoryTrackStructureChange& change) const;
    // Trusted no-fail tail for an unchanged, admitted Track Structure entry.
    // Shared topology has already been published by the owning transaction.
    void commitAdmittedSequencerStructureHistory(
        sequencer::SequencerHistoryTrackStructureChangePtr change
    ) noexcept;
    sequencer::SequencerHistoryOpenOutcome beginOrContinueSequencerPatternHistoryCoalescing(
        uint8_t step, sequencer::StepProperty property, uint32_t nowMs,
        sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan, bool stateProperty = false);
    bool sealSequencerPatternHistoryCoalescing(bool mutationChanged);
    sequencer::SequencerPreparedPatternEditBeginOutcome beginOrContinueSequencerPreparedPatternEdit(
        sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
        sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
        sequencer::SequencerHistoryDescriptor descriptor, bool compactGraphOnSeal = false);
    [[nodiscard]] bool sequencerPreparedPatternEditReady(
        sequencer::SequencerPreparedPatternEditOwner owner,
        uint8_t key,
        uint8_t expectedTrack) const;
    [[nodiscard]] sequencer::SequencerPreparedPatternGraphPrecompactionOutcome
    precompactSequencerPreparedPatternEditGraph(
        sequencer::SequencerPreparedPatternEditOwner owner,
        uint8_t key,
        uint8_t expectedTrack,
        sequencer::SequencerPreparedGraphContentPath& contentPath);
    sequencer::SequencerPreparedPatternEditSealOutcome sealSequencerPreparedPatternEdit(
        sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key, bool mutationChanged,
        sequencer::SequencerHistoryDescriptor descriptor);
    sequencer::SequencerPreparedPatternEditCommitOutcome commitSequencerPreparedPatternEdit(
        sequencer::SequencerPreparedPatternEditOwner owner);
    // Captures/admit the detached Quick Controls candidate before swapping its
    // payload into live state and publishing through the normal prepared sink.
    sequencer::SequencerPreparedPatternEditCommitOutcome
    applySequencerPreparedQuickControlsEdit(
        uint8_t key,
        sequencer::SequencerHistoryDescriptor descriptor);
    // Matching prepared-family owners are restored through the single
    // allocation-free rollback primitive, before or after seal.
    [[nodiscard]] sequencer::SequencerPreparedPatternEditAbortOutcome
    abortSequencerPreparedPatternEdit(
        sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key);
    sequencer::SequencerHistoryOpenOutcome beginOrContinueSequencerCcLaneEventHistoryCoalescing(
        uint8_t lane, uint8_t step, int32_t beforeValue, int32_t afterValue,
        const sequencer::SequencerCcLaneBank* afterBank, uint32_t nowMs);
    sequencer::SequencerPatternHistoryCommitOutcome
    commitSequencerPatternHistoryCoalescingOutcome();
    /**
     * Opens the single Track Structure chronology boundary.
     *
     * Pending Macro auditions and Project-Track gestures reject the boundary.
     * A pending Sequencer Pattern owner is then committed with a checked
     * outcome before the remaining Project mutation coalescers are closed by
     * CoreStateLifecycle. The returned Pattern outcome lets the Track
     * transaction report that predecessor publication independently.
     */
    [[nodiscard]] sequencer::SequencerTrackStructureChronologyResult
    openSequencerTrackStructureChronologyBoundary();
    bool commitSequencerPatternHistoryCoalescing();
    bool updateSequencerPatternHistoryCoalescing(uint32_t nowMs);
    bool hasPendingSequencerPatternHistoryCoalescing() const;
    bool undoSequencerHistory();
    bool redoSequencerHistory();
    [[nodiscard]] bool clearSequencerHistory();
    [[nodiscard]] bool prepareProjectHistoryInteraction();
    bool undoProjectHistory();
    bool redoProjectHistory();
    [[nodiscard]] bool clearProjectHistory();
    [[nodiscard]] bool queuePendingSequencerApply(sequencer::SequencerState& staged,
                                                  bool merge = false);
    [[nodiscard]] bool queuePendingSequencerBankApply(
        sequencer::SequencerTrackBankState& stagedBank, sequencer::SequencerState& staged);
    void clearPendingSequencerApply();
    bool hasPendingSequencerApply() const;
    uint16_t currentSharedTrackEnabledMask() const;
    uint8_t currentSharedActiveTrack() const;
    bool setSharedTrackState(uint16_t enabledMask, uint8_t activeTrack);
    [[nodiscard]] bool publishPreparedSequencerTrackState(uint16_t enabledMask,
                                                          uint8_t activeTrack);
    /** Finalizes Macro presentation after a prepared Sequencer active-Track change. */
    void reconcilePreparedSequencerActiveTrackPresentation() noexcept;
    /** Reconciles transient Macro/UI state after one atomic global Track paste. */
    void reconcilePreparedMacroTrackTransfer(uint16_t capturedTrackMask);
    bool refreshSharedTrackStateFromMacroPages();
    bool refreshSharedTrackStateFromSequencer();

    /**
     * @brief Save current device settings after reopening their storage
     *
     * Project and preset files are independent ProductFileService domains and
     * are not rewritten by this recovery path.
     */
    persistence::PersistenceWriteStatus recoverSettingsFromRamAfterStorageReopen();
private:
    using SequencerPatternHistoryCommitOutcome = sequencer::SequencerPatternHistoryCommitOutcome;

    void consumePendingSequencerMutation_(bool* priorMutation = nullptr);
    void clearPreparedSequencerPatternEditWithoutLiveRestore_();
    bool rollbackPreparedSequencerPatternEdit_();
    sequencer::SequencerPreparedPatternEditSealOutcome
    sealSequencerPreparedPatternEditWithGraphCompaction_(
        sequencer::SequencerHistoryDescriptor descriptor);
    sequencer::SequencerPreparedPatternEditSealOutcome finishSequencerPreparedPatternEdit_(
        sequencer::SequencerHistoryDescriptor descriptor,
        const sequencer::SequencerGraphCompactionRemap* compactionRemap, bool graphCompacted);
    SequencerPatternHistoryCommitOutcome abandonUnsafeSequencerPatternHistory_(const char* reason);
    SequencerPatternHistoryCommitOutcome commitSequencerPatternHistoryCoalescing_();
    [[nodiscard]] bool queueSequencerApply_(sequencer::SequencerState& staged, bool merge = false);
    [[nodiscard]] bool queueSequencerBankApply_(sequencer::SequencerTrackBankState& stagedBank,
                                                sequencer::SequencerState& staged);
    void requestProjectSessionSave_();
    void markProjectDurableMutation_();
    void markSequencerProjectMutated_();
    void clearPendingSequencerApply_();
    bool refreshSharedTrackStateFromMacroPages_();
    bool refreshSharedTrackStateFromSequencer_();
    bool setSharedTrackState_(uint16_t enabledMask, uint8_t activeTrack);
    bool traverseSequencerHistory_(sequencer::SequencerHistoryDirection direction);
    bool traversePreparedSequencerStructureHistory_(
        sequencer::SequencerHistoryDirection direction,
        sequencer::SequencerPreparedStructureHistoryReplay&& prepared);
    bool traverseGenericSequencerHistory_(
        sequencer::SequencerHistoryDirection direction);
    bool armPreparedSequencerHistoryActivation_(
        sequencer::SequencerHistoryDirection direction,
        const sequencer::SequencerTrackActivationHistoryPlan& activation,
        sequencer::SequencerTrackActivationHistoryTransition& transition);
    void publishSequencerHistoryTraversal_(
        const sequencer::SequencerHistoryApplyResult& result,
        const sequencer::SequencerHistoryMacroTrackStructurePayload* macroStructure,
        const sequencer::SequencerTrackActivationHistoryPlan& activation,
        const sequencer::SequencerTrackActivationHistoryTransition& transition,
        bool hasActivation,
        uint8_t activeTrackBefore);
};

}  // namespace core::state

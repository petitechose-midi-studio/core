#pragma once

#include "state/macro/MacroHistoryTypes.hpp"

namespace core::state::macro {

[[nodiscard]] bool captureMacroSlotHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroSlotHistorySnapshot& out
);

[[nodiscard]] bool sameMacroSlotHistorySnapshot(
    const MacroSlotHistorySnapshot& lhs,
    const MacroSlotHistorySnapshot& rhs
);

[[nodiscard]] bool liveMacroSlotMatchesHistorySnapshot(
    const MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
);

/** Applies a validated Slot snapshot without partially mutating on failure. */
[[nodiscard]] bool applyMacroSlotHistorySnapshot(
    MacroPagesState& pages,
    const MacroSlotHistorySnapshot& snapshot
);

[[nodiscard]] bool captureMacroAutomationHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address,
    MacroAutomationHistorySnapshot& out
);

[[nodiscard]] bool sameMacroAutomationHistorySnapshot(
    const MacroAutomationHistorySnapshot& lhs,
    const MacroAutomationHistorySnapshot& rhs
);

[[nodiscard]] bool liveMacroAutomationMatchesHistorySnapshot(
    const MacroPagesState& pages,
    const MacroAutomationHistorySnapshot& snapshot
);

/** Applies only absolute Automation and preserves every Modulation object. */
[[nodiscard]] bool applyMacroAutomationHistorySnapshot(
    MacroPagesState& pages,
    const MacroAutomationHistorySnapshot& snapshot
);

class MacroHistoryService {
public:
    static constexpr uint8_t ENTRY_LIMIT = 8;

    MacroHistoryService();
    ~MacroHistoryService();
    MacroHistoryService(const MacroHistoryService&) = delete;
    MacroHistoryService& operator=(const MacroHistoryService&) = delete;

    void setProjectHistoryEventSink(
        const core::state::project::ProjectHistoryEventSink* sink
    ) {
        project_history_sink_ = sink;
    }

    [[nodiscard]] MacroHistoryChangePtr prepare(
        const MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        MacroHistoryActionKind kind
    ) const;

    /** Captures only absolute Automation for a performance recording commit. */
    [[nodiscard]] MacroHistoryChangePtr prepareAutomationRecording(
        const MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    ) const;

    /** Reserves every possible lane and Redo point array before take t0. */
    [[nodiscard]] MacroHistoryChangePtr prepareAutomationTake(
        const MacroPagesState& pages,
        uint8_t track,
        uint8_t page,
        uint16_t candidateMask
    ) const;

    /** Admits one already-published, allocation-free take transaction. */
    [[nodiscard]] bool commitPreparedAutomationTake(
        MacroPagesState& pages,
        MacroHistoryChangePtr& change
    );

    /**
     * Captures the post-state and records one action. On capture/admission
     * failure the pre-state is restored before returning false.
     */
    [[nodiscard]] bool commitPrepared(
        MacroPagesState& pages,
        MacroHistoryChangePtr change,
        bool coalesce = false
    );

    [[nodiscard]] MacroHistoryChangePtr prepareTrackConfig(
        const MacroPagesState& pages,
        const core::state::project::ProjectTrackState& projectTracks,
        uint8_t track,
        uint8_t page
    ) const;
    [[nodiscard]] bool commitPreparedTrackConfig(
        MacroPagesState& pages,
        core::state::project::ProjectTrackState& projectTracks,
        MacroHistoryChangePtr change
    );

    /** Applies one contiguous Page compaction and records it as one action. */
    [[nodiscard]] bool compactPages(
        MacroPagesState& pages,
        uint8_t track,
        uint16_t retainedPageMask
    );

    /** Reserves exact before/after storage for any non-compacting Page edit. */
    [[nodiscard]] MacroHistoryChangePtr preparePageStructureSnapshot(
        const MacroPagesState& pages,
        uint8_t track
    ) const;
    [[nodiscard]] bool commitPreparedPageStructureSnapshot(
        MacroPagesState& pages,
        MacroHistoryChangePtr change
    );

    /**
     * Reserves history before publishing one provisional LFO + assignment.
     * The returned IDs are also projected through ProjectControlState::audition.
     */
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        beginLfoModulatorAudition(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress& address,
            const core::state::modulation::ModulatorLfoDraft& sourceDraft,
            const core::state::modulation::ModulationBindingDraft& bindingDraft,
            bool createMacroSlot = false,
            const MacroDestinationActivationPlan* destinationPlan = nullptr
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        beginAdsrModulatorAudition(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress& address,
            const core::state::modulation::ModulatorAdsrDraft& sourceDraft,
            const core::state::modulation::ModulationTriggerDraft& triggerDraft,
            const core::state::modulation::ModulationBindingDraft& bindingDraft,
            bool createMacroSlot = false,
            const MacroDestinationActivationPlan* destinationPlan = nullptr
        );

    /** Creates one explicit detached LFO as one compact Undo action. */
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        createUnassignedLfo(
            MacroPagesState& pages,
            const core::state::modulation::ModulatorLfoDraft& sourceDraft
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        createUnassignedAdsr(
            MacroPagesState& pages,
            const core::state::modulation::ModulatorAdsrDraft& sourceDraft,
            const core::state::modulation::ModulationTriggerDraft& triggerDraft
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        createUnassignedRecordedShape(
            MacroPagesState& pages,
            const core::state::modulation::RecordedShapeDraft& sourceDraft
        );

    /** Creates one Recorded Shape and its destination as one exact Undo. */
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        createAssignedRecordedShape(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress& address,
            const core::state::modulation::RecordedShapeDraft& sourceDraft,
            const core::state::modulation::ModulationBindingDraft& bindingDraft,
            bool createMacroSlot = false,
            const MacroDestinationActivationPlan* destinationPlan = nullptr
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        duplicateProjectModulator(
            MacroPagesState& pages,
            core::state::modulation::ModulatorId sourceId,
            const char* cloneName
        );

    /** Reserves and auditions one edge to a pre-existing Project source. */
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        beginExistingModulatorAudition(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress& address,
            core::state::modulation::ModulatorId sourceId,
            const core::state::modulation::ModulationBindingDraft& bindingDraft,
            bool createMacroSlot = false,
            const MacroDestinationActivationPlan* destinationPlan = nullptr
        );

    /** Exact rollback with no Undo entry and no authored ID/capacity residue. */
    [[nodiscard]] bool cancelModulatorAudition(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    );

    /** Publishes the reserved delta as one stable-ID Undo action. */
    [[nodiscard]] bool commitModulatorAudition(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    );

    [[nodiscard]] bool modulatorAuditionPending(
        const MacroAutomationSlotAddress& address
    ) const;

    /** Fail-closed transaction predicate, including inconsistent transient state. */
    [[nodiscard]] bool hasPendingModulatorAuditionTransaction(
        const MacroPagesState& pages
    ) const;

    /** Exact lifecycle rollback before a history boundary destroys pending state. */
    [[nodiscard]] bool abortPendingModulatorAudition(MacroPagesState& pages);

    /** Depth edit fast path: one allocation on first turn, none while coalescing. */
    [[nodiscard]] bool setModulationDepthCoalesced(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        float depth
    );

    /** One allocation on the first delta, then in-place gesture coalescing. */
    [[nodiscard]] bool setMacroValueCoalesced(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        float value
    );

    /** Timing-only fast paths: one compact history entry per encoder gesture. */
    [[nodiscard]] bool setAutomationDurationBeatsCoalesced(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        float durationBeats
    );
    [[nodiscard]] bool setAutomationWindowOffsetBeatsCoalesced(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        float offsetBeats
    );

    /** One takeover gesture: Manual authority and optional Base edit together. */
    [[nodiscard]] bool setManualOverrideCoalesced(
        MacroPagesState& pages,
        MacroManualOverrideState& overrides,
        const MacroAutomationSlotAddress& address,
        float value,
        bool coalesceValue
    );

    /** One discrete command that hands authority back to computed sources. */
    [[nodiscard]] bool resumeManualOverride(
        MacroPagesState& pages,
        MacroManualOverrideState& overrides,
        const MacroAutomationSlotAddress& address
    );

    /** Signed Depth edit for one stable assignment; coalesced per gesture. */
    [[nodiscard]] bool setModulationBindingDepthCoalesced(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        core::state::modulation::ModulationBindingId bindingId,
        float depth
    );

    /** Destination-wide 0..200% multiplier; coalesced without edge snapshots. */
    [[nodiscard]] bool setModulationDestinationScaleCoalesced(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        uint16_t scaleQ15
    );

    [[nodiscard]] bool setModulationBindingEnabled(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        core::state::modulation::ModulationBindingId bindingId,
        bool enabled
    );

    [[nodiscard]] bool setAllModulationBindingsEnabled(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        bool enabled
    );

    [[nodiscard]] bool removeModulationBinding(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        core::state::modulation::ModulationBindingId bindingId
    );

    [[nodiscard]] bool clearModulationBindings(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    );

    /** Removes one physical Macro and every authored destination-owned value. */
    [[nodiscard]] bool removeMacroSlot(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address
    );

    /** Adds or updates one typed shared-source assignment as one Undo action. */
    [[nodiscard]] bool pasteModulationBinding(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        const core::state::modulation::ModulationBindingDraft& draft,
        bool overwriteExisting,
        core::state::modulation::ModulationBindingId* appliedBinding = nullptr
    );

    [[nodiscard]] bool setProjectModulatorEnabled(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        bool enabled
    );
    [[nodiscard]] bool setProjectModulatorName(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        const char* name
    );
    [[nodiscard]] bool setProjectLfoParametersCoalesced(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        const core::state::modulation::ModulatorLfoParameters& parameters
    );
    [[nodiscard]] bool setProjectAdsrParametersCoalesced(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        const core::state::modulation::ModulatorAdsrParameters& parameters
    );
    /** Re-records one source, preserving stable IDs or applying exact COW. */
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        replaceProjectRecordedShapeCurve(
            MacroPagesState& pages,
            core::state::modulation::ModulatorId sourceId,
            const core::state::modulation::ProjectCurveSpec& curve,
            const core::state::modulation::ProjectPackedCurvePoint* points,
            uint16_t pointCount
        );
    [[nodiscard]] bool setProjectModulationTriggerCoalesced(
        MacroPagesState& pages,
        core::state::modulation::ModulatorId sourceId,
        const core::state::modulation::ModulationTriggerFilter& trigger,
        bool enabled,
        uint8_t velocityMin = 0U,
        uint8_t velocityMax = 127U
    );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        splitProjectModulator(
            MacroPagesState& pages,
            const core::state::modulation::ModulatorSplitRequest& request
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        splitProjectModulatorTrack(
            MacroPagesState& pages,
            core::state::modulation::ModulatorId sourceId,
            uint8_t track,
            const char* cloneName
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        deleteProjectModulator(
            MacroPagesState& pages,
            core::state::modulation::ModulatorId sourceId
        );

    void endCoalescing();
    [[nodiscard]] bool undo(
        MacroPagesState& pages,
        MacroAutomationSlotAddress* appliedAddress = nullptr,
        MacroManualOverrideState* manualOverrides = nullptr,
        core::state::project::ProjectTrackState* projectTracks = nullptr
    );
    [[nodiscard]] bool redo(
        MacroPagesState& pages,
        MacroAutomationSlotAddress* appliedAddress = nullptr,
        MacroManualOverrideState* manualOverrides = nullptr,
        core::state::project::ProjectTrackState* projectTracks = nullptr
    );
    void clear();

    [[nodiscard]] bool canUndo() const { return undo_count_ > 0; }
    [[nodiscard]] bool canRedo() const { return redo_count_ > 0; }
    [[nodiscard]] uint8_t undoCount() const { return undo_count_; }
    [[nodiscard]] uint8_t redoCount() const { return redo_count_; }
    [[nodiscard]] uintptr_t projectHistoryUndoIdentity() const {
        return undo_count_ > 0U && undo_[undo_count_ - 1U]
            ? reinterpret_cast<uintptr_t>(undo_[undo_count_ - 1U].get())
            : 0U;
    }
    [[nodiscard]] uintptr_t projectHistoryRedoIdentity() const {
        return redo_count_ > 0U && redo_[redo_count_ - 1U]
            ? reinterpret_cast<uintptr_t>(redo_[redo_count_ - 1U].get())
            : 0U;
    }
    [[nodiscard]] bool projectHistoryUndoTouchesDurableState() const {
        const auto* change = undo_count_ > 0U
            ? undo_[undo_count_ - 1U].get()
            : nullptr;
        return change != nullptr &&
               (change->kind != MacroHistoryActionKind::MANUAL_OVERRIDE_STATE ||
                change->valueEdit.valid);
    }
    [[nodiscard]] bool projectHistoryRedoTouchesDurableState() const {
        const auto* change = redo_count_ > 0U
            ? redo_[redo_count_ - 1U].get()
            : nullptr;
        return change != nullptr &&
               (change->kind != MacroHistoryActionKind::MANUAL_OVERRIDE_STATE ||
                change->valueEdit.valid);
    }
    void discardRedoBranch();

private:
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        beginNewModulatorAudition_(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress& address,
            const core::state::modulation::ModulatorLfoDraft* lfoDraft,
            const core::state::modulation::ModulatorAdsrDraft* adsrDraft,
            const core::state::modulation::ModulationTriggerDraft* triggerDraft,
            const core::state::modulation::ModulationBindingDraft& bindingDraft,
            bool createMacroSlot,
            const MacroDestinationActivationPlan* destinationPlan
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        createUnassignedModulator_(
            MacroPagesState& pages,
            const core::state::modulation::ModulatorLfoDraft* lfoDraft,
            const core::state::modulation::ModulatorAdsrDraft* adsrDraft,
            const core::state::modulation::ModulationTriggerDraft* triggerDraft
        );
    [[nodiscard]] core::state::modulation::ProjectModulationResult
        createRecordedShape_(
            MacroPagesState& pages,
            const MacroAutomationSlotAddress* address,
            const core::state::modulation::RecordedShapeDraft& sourceDraft,
            const core::state::modulation::ModulationBindingDraft* bindingDraft,
            bool createMacroSlot,
            const MacroDestinationActivationPlan* destinationPlan
        );
    [[nodiscard]] MacroHistoryChangePtr prepareModulationAssignments_(
        const MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        MacroHistoryActionKind kind
    ) const;
    [[nodiscard]] bool commitModulationAssignments_(
        MacroPagesState& pages,
        MacroHistoryChangePtr change,
        bool coalesce = false
    );
    [[nodiscard]] bool commitProjectSourceEdit_(
        MacroPagesState& pages,
        MacroHistoryChangePtr change,
        bool coalesce
    );
    [[nodiscard]] bool setAutomationMetadataCoalesced_(
        MacroPagesState& pages,
        const MacroAutomationSlotAddress& address,
        const core::state::modulation::ProjectCurveSpec& spec,
        MacroHistoryActionKind kind
    );
    [[nodiscard]] MacroHistoryChangePtr* pendingModulatorSlot_();
    [[nodiscard]] const MacroHistoryChangePtr* pendingModulatorSlot_() const;
    [[nodiscard]] bool parkPending_(MacroHistoryChangePtr change);
    [[nodiscard]] MacroHistoryChangePtr takePending_();
    static void push_(
        std::array<MacroHistoryChangePtr, ENTRY_LIMIT>& stack,
        uint8_t& count,
        MacroHistoryChangePtr change,
        const core::state::project::ProjectHistoryEventSink* sink
    );
    void recordNewEntry_(MacroHistoryChangePtr change);
    void clearRedo_();

    std::array<MacroHistoryChangePtr, ENTRY_LIMIT> undo_{};
    std::array<MacroHistoryChangePtr, ENTRY_LIMIT> redo_{};
    uint8_t undo_count_ = 0;
    uint8_t redo_count_ = 0;
    bool coalescing_ = false;
    MacroHistoryActionKind coalesced_kind_ = MacroHistoryActionKind::SOURCE_STATE;
    MacroAutomationSlotAddress coalesced_address_{};
    const core::state::project::ProjectHistoryEventSink* project_history_sink_ = nullptr;
};

}  // namespace core::state::macro

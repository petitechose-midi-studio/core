#pragma once

#include "state/sequencer/SequencerHistory.hpp"

namespace core::state {
struct CoreState;
}

namespace core::state::sequencer {
struct SequencerPreparedGraphContentPath;
}

namespace core::handler {

class SequencerHistoryDomainServices {
public:
    using CanRecordPatternFn = bool (*)(
        void* context, const core::state::sequencer::SequencerHistoryPatternChange& change);
    using RecordPreparedPatternFn =
        void (*)(void* context, core::state::sequencer::SequencerHistoryPatternChangePtr change);
    using CanRecordStructureFn = bool (*)(
        const void* context,
        const core::state::sequencer::SequencerHistoryTrackStructureChange& change);
    using CommitAdmittedStructureFn = void (*)(
        void* context,
        core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
    ) noexcept;
    using BeginCoalescedPatternEditFn = core::state::sequencer::SequencerHistoryOpenOutcome (*)(
        void* context, uint8_t step, core::state::sequencer::StepProperty property, uint32_t nowMs,
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
        bool stateProperty);
    using SealCoalescedPatternEditFn = bool (*)(void* context, bool mutationChanged);
    using BeginCoalescedCcLaneEventEditFn = core::state::sequencer::SequencerHistoryOpenOutcome (*)(void* context, uint8_t lane, uint8_t step, int32_t beforeValue, int32_t afterValue,
                 const core::state::sequencer::SequencerCcLaneBank* afterBank, uint32_t nowMs);
    using CommitCoalescedPatternEditFn =
        core::state::sequencer::SequencerPatternHistoryCommitOutcome (*)(void* context);
    using OpenTrackStructureChronologyBoundaryFn =
        core::state::sequencer::SequencerTrackStructureChronologyResult (*)(
            void* context);
    using BeginPreparedPatternEditFn =
        core::state::sequencer::SequencerPreparedPatternEditBeginOutcome (*)(
            void* context, core::state::sequencer::SequencerPreparedPatternEditOwner owner,
            uint8_t key, core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
            core::state::sequencer::SequencerHistoryDescriptor descriptor, bool compactGraphOnSeal);
    using PreparedPatternEditReadyFn = bool (*)(
        void* context,
        core::state::sequencer::SequencerPreparedPatternEditOwner owner,
        uint8_t key,
        uint8_t expectedTrack);
    using PrecompactPreparedPatternEditGraphFn =
        core::state::sequencer::SequencerPreparedPatternGraphPrecompactionOutcome (*)(
            void* context,
            core::state::sequencer::SequencerPreparedPatternEditOwner owner,
            uint8_t key,
            uint8_t expectedTrack,
            core::state::sequencer::SequencerPreparedGraphContentPath& contentPath);
    using SealPreparedPatternEditFn =
        core::state::sequencer::SequencerPreparedPatternEditSealOutcome (*)(
            void* context, core::state::sequencer::SequencerPreparedPatternEditOwner owner,
            uint8_t key, bool mutationChanged,
            core::state::sequencer::SequencerHistoryDescriptor descriptor);
    using CommitPreparedPatternEditFn =
        core::state::sequencer::SequencerPreparedPatternEditCommitOutcome (*)(
            void* context, core::state::sequencer::SequencerPreparedPatternEditOwner owner);
    using AbortPreparedPatternEditFn =
        core::state::sequencer::SequencerPreparedPatternEditAbortOutcome (*)(
            void* context, core::state::sequencer::SequencerPreparedPatternEditOwner owner,
            uint8_t key);
    using ApplyPreparedProjectScaleChoiceFn =
        core::state::sequencer::SequencerPreparedFullBankEditResult (*)(
            void* context,
            core::state::sequencer::SequencerPreparedFullBankEditOwner owner,
            uint8_t row,
            int choiceIndex);
    using ApplyPreparedQuickControlsEditFn =
        core::state::sequencer::SequencerPreparedPatternEditCommitOutcome (*)(
            void* context,
            uint8_t key,
            core::state::sequencer::SequencerHistoryDescriptor descriptor);

    struct Operations {
        CanRecordPatternFn canRecordPattern = nullptr;
        RecordPreparedPatternFn recordPreparedPattern = nullptr;
        CanRecordStructureFn canRecordStructure = nullptr;
        CommitAdmittedStructureFn commitAdmittedStructure = nullptr;
        BeginCoalescedPatternEditFn beginCoalescedPatternEdit = nullptr;
        SealCoalescedPatternEditFn sealCoalescedPatternEdit = nullptr;
        BeginCoalescedCcLaneEventEditFn beginCoalescedCcLaneEventEdit = nullptr;
        CommitCoalescedPatternEditFn commitCoalescedPatternEdit = nullptr;
        OpenTrackStructureChronologyBoundaryFn
            openTrackStructureChronologyBoundary = nullptr;
        BeginPreparedPatternEditFn beginPreparedPatternEdit = nullptr;
        PreparedPatternEditReadyFn preparedPatternEditReady = nullptr;
        PrecompactPreparedPatternEditGraphFn precompactPreparedPatternEditGraph = nullptr;
        SealPreparedPatternEditFn sealPreparedPatternEdit = nullptr;
        CommitPreparedPatternEditFn commitPreparedPatternEdit = nullptr;
        AbortPreparedPatternEditFn abortPreparedPatternEdit = nullptr;
        ApplyPreparedProjectScaleChoiceFn applyPreparedProjectScaleChoice = nullptr;
        ApplyPreparedQuickControlsEditFn applyPreparedQuickControlsEdit = nullptr;
    };

    SequencerHistoryDomainServices() = default;
    // A reference non-type template argument can only designate static storage.
    // Keep the facade compact without permitting a dangling operations table.
    template <const Operations& operations>
    static SequencerHistoryDomainServices fromStaticOperations(void* context) {
        return SequencerHistoryDomainServices(context, &operations);
    }
    static SequencerHistoryDomainServices fromCoreState(core::state::CoreState& state);

    bool canRecordPattern(
        const core::state::sequencer::SequencerHistoryPatternChange& change) const;
    // Precondition: canRecordPattern(change) was true and change is unchanged.
    void recordPreparedPattern(
        core::state::sequencer::SequencerHistoryPatternChangePtr change) const;
    [[nodiscard]] bool canRecordStructure(
        const core::state::sequencer::SequencerHistoryTrackStructureChange& change) const;
    // Trusted Track transaction gate. A true result admits the unchanged
    // payload and proves the no-fail Core commit sink is installed.
    [[nodiscard]] bool canCommitAdmittedStructure(
        const core::state::sequencer::SequencerHistoryTrackStructureChange& change) const;
    // Precondition: canCommitAdmittedStructure(change) was true and change is
    // unchanged. Missing authority is an invariant failure, never a soft tail.
    void commitAdmittedStructure(
        core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
    ) const noexcept;
    core::state::sequencer::SequencerHistoryOpenOutcome beginCoalescedPatternEdit(
        uint8_t step, core::state::sequencer::StepProperty property, uint32_t nowMs,
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
        bool stateProperty = false) const;
    bool sealCoalescedPatternEdit(bool mutationChanged) const;
    core::state::sequencer::SequencerHistoryOpenOutcome beginCoalescedCcLaneEventEdit(uint8_t lane, uint8_t step, int32_t beforeValue,
                                       int32_t afterValue,
                                       const core::state::sequencer::SequencerCcLaneBank* afterBank,
                                       uint32_t nowMs) const;
    core::state::sequencer::SequencerPatternHistoryCommitOutcome commitCoalescedPatternEditOutcome()
        const;
    [[nodiscard]] core::state::sequencer::SequencerTrackStructureChronologyResult
    openTrackStructureChronologyBoundary() const;
    core::state::sequencer::SequencerPreparedPatternEditBeginOutcome beginPreparedPatternEdit(
        core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
        core::state::sequencer::SequencerHistoryDescriptor descriptor = {},
        bool compactGraphOnSeal = false) const;
    [[nodiscard]] bool preparedPatternEditReady(
        core::state::sequencer::SequencerPreparedPatternEditOwner owner,
        uint8_t key,
        uint8_t expectedTrack) const;
    [[nodiscard]] core::state::sequencer::
        SequencerPreparedPatternGraphPrecompactionOutcome
    precompactPreparedPatternEditGraph(
        core::state::sequencer::SequencerPreparedPatternEditOwner owner,
        uint8_t key,
        uint8_t expectedTrack,
        core::state::sequencer::SequencerPreparedGraphContentPath& contentPath) const;
    core::state::sequencer::SequencerPreparedPatternEditSealOutcome sealPreparedPatternEdit(
        core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key,
        bool mutationChanged,
        core::state::sequencer::SequencerHistoryDescriptor descriptor = {}) const;
    core::state::sequencer::SequencerPreparedPatternEditCommitOutcome commitPreparedPatternEdit(
        core::state::sequencer::SequencerPreparedPatternEditOwner owner) const;
    [[nodiscard]] core::state::sequencer::SequencerPreparedPatternEditAbortOutcome
    abortPreparedPatternEdit(
        core::state::sequencer::SequencerPreparedPatternEditOwner owner, uint8_t key) const;
    core::state::sequencer::SequencerPreparedFullBankEditResult
    applyPreparedProjectScaleChoice(
        core::state::sequencer::SequencerPreparedFullBankEditOwner owner,
        uint8_t row,
        int choiceIndex
    ) const;
    core::state::sequencer::SequencerPreparedPatternEditCommitOutcome
    applyPreparedQuickControlsEdit(
        uint8_t key,
        core::state::sequencer::SequencerHistoryDescriptor descriptor = {}) const;
private:
    SequencerHistoryDomainServices(void* context, const Operations* operations);
    static const Operations EMPTY_OPERATIONS;

    void* context_ = nullptr;
    const Operations* operations_ = &EMPTY_OPERATIONS;
};

static_assert(sizeof(SequencerHistoryDomainServices) == sizeof(void*) * 2U,
              "history domain facade must remain two pointers");

}  // namespace core::handler

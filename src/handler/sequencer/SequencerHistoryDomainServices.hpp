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
    using RecordPatternFn = bool (*)(void* context,
                                     core::state::sequencer::SequencerHistoryPatternSnapshot before,
                                     core::state::sequencer::SequencerHistoryPatternSnapshot after,
                                     core::state::sequencer::SequencerHistoryDescriptor descriptor);
    using RecordPatternChangeFn =
        bool (*)(void* context, core::state::sequencer::SequencerHistoryPatternChangePtr change);
    using CanRecordPatternFn = bool (*)(
        void* context, const core::state::sequencer::SequencerHistoryPatternChange& change);
    using RecordPreparedPatternFn =
        void (*)(void* context, core::state::sequencer::SequencerHistoryPatternChangePtr change);
    using RecordFullBankFn =
        bool (*)(void* context, core::state::sequencer::SequencerHistoryFullBankChangePtr change);
    using CanRecordFullBankFn = bool (*)(
        void* context, const core::state::sequencer::SequencerHistoryFullBankChange& change);
    using RecordPreparedFullBankFn =
        void (*)(void* context, core::state::sequencer::SequencerHistoryFullBankChangePtr change);
    using RecordStructureFn = bool (*)(
        void* context, core::state::sequencer::SequencerHistoryTrackStructureChangePtr change);
    using CanRecordStructureFn = bool (*)(
        void* context, const core::state::sequencer::SequencerHistoryTrackStructureChange& change);
    using RecordPreparedStructureFn = void (*)(
        void* context, core::state::sequencer::SequencerHistoryTrackStructureChangePtr change);
    using BeginCoalescedPatternEditFn = bool (*)(
        void* context, uint8_t step, core::state::sequencer::StepProperty property, uint32_t nowMs,
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
        bool stateProperty);
    using SealCoalescedPatternEditFn = bool (*)(void* context, bool mutationChanged);
    using BeginCoalescedCcLaneEventEditFn =
        bool (*)(void* context, uint8_t lane, uint8_t step, int32_t beforeValue, int32_t afterValue,
                 const core::state::sequencer::SequencerCcLaneBank* afterBank, uint32_t nowMs);
    using CommitCoalescedPatternEditFn =
        core::state::sequencer::SequencerPatternHistoryCommitOutcome (*)(void* context);
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

    struct Operations {
        RecordPatternFn recordPattern = nullptr;
        RecordPatternFn recordFlatPattern = nullptr;
        RecordPatternChangeFn recordPatternChange = nullptr;
        CanRecordPatternFn canRecordPattern = nullptr;
        RecordPreparedPatternFn recordPreparedPattern = nullptr;
        RecordPreparedPatternFn recordPreparedSynchronizedPattern = nullptr;
        RecordStructureFn recordStructure = nullptr;
        CanRecordStructureFn canRecordStructure = nullptr;
        RecordPreparedStructureFn recordPreparedStructure = nullptr;
        RecordFullBankFn recordFullBank = nullptr;
        CanRecordFullBankFn canRecordFullBank = nullptr;
        RecordPreparedFullBankFn recordPreparedFullBank = nullptr;
        BeginCoalescedPatternEditFn beginCoalescedPatternEdit = nullptr;
        SealCoalescedPatternEditFn sealCoalescedPatternEdit = nullptr;
        BeginCoalescedCcLaneEventEditFn beginCoalescedCcLaneEventEdit = nullptr;
        CommitCoalescedPatternEditFn commitCoalescedPatternEdit = nullptr;
        BeginPreparedPatternEditFn beginPreparedPatternEdit = nullptr;
        PreparedPatternEditReadyFn preparedPatternEditReady = nullptr;
        PrecompactPreparedPatternEditGraphFn precompactPreparedPatternEditGraph = nullptr;
        SealPreparedPatternEditFn sealPreparedPatternEdit = nullptr;
        CommitPreparedPatternEditFn commitPreparedPatternEdit = nullptr;
        AbortPreparedPatternEditFn abortPreparedPatternEdit = nullptr;
        ApplyPreparedProjectScaleChoiceFn applyPreparedProjectScaleChoice = nullptr;
    };

    SequencerHistoryDomainServices() = default;
    // A reference non-type template argument can only designate static storage.
    // Keep the facade compact without permitting a dangling operations table.
    template <const Operations& operations>
    static SequencerHistoryDomainServices fromStaticOperations(void* context) {
        return SequencerHistoryDomainServices(context, &operations);
    }
    static SequencerHistoryDomainServices fromCoreState(core::state::CoreState& state);

    bool recordPattern(core::state::sequencer::SequencerHistoryPatternSnapshot before,
                       core::state::sequencer::SequencerHistoryPatternSnapshot after,
                       core::state::sequencer::SequencerHistoryDescriptor descriptor = {}) const;
    bool recordFlatPattern(
        core::state::sequencer::SequencerHistoryPatternSnapshot before,
        core::state::sequencer::SequencerHistoryPatternSnapshot after,
        core::state::sequencer::SequencerHistoryDescriptor descriptor = {}) const;
    bool recordPattern(core::state::sequencer::SequencerHistoryPatternChangePtr change) const;
    bool canRecordPattern(
        const core::state::sequencer::SequencerHistoryPatternChange& change) const;
    // Precondition: canRecordPattern(change) was true and change is unchanged.
    void recordPreparedPattern(
        core::state::sequencer::SequencerHistoryPatternChangePtr change) const;
    // Same admission contract, but the caller has already published an exact
    // editor-to-bank synchronization and may consume the generic coalescer.
    bool canRecordSynchronizedPattern(
        const core::state::sequencer::SequencerHistoryPatternChange& change) const;
    void recordPreparedSynchronizedPattern(
        core::state::sequencer::SequencerHistoryPatternChangePtr change) const;
    bool recordFullBank(core::state::sequencer::SequencerHistoryFullBankChangePtr change) const;
    bool canRecordFullBank(
        const core::state::sequencer::SequencerHistoryFullBankChange& change) const;
    // Precondition: canRecordFullBank(change) was true and change is unchanged.
    void recordPreparedFullBank(
        core::state::sequencer::SequencerHistoryFullBankChangePtr change) const;
    bool canRecordStructure(
        const core::state::sequencer::SequencerHistoryTrackStructureChange& change) const;
    // Precondition: canRecordStructure(change) was true and change is unchanged.
    void recordPreparedStructure(
        core::state::sequencer::SequencerHistoryTrackStructureChangePtr change) const;
    bool recordStructure(
        core::state::sequencer::SequencerHistoryTrackStructureChangePtr change) const;
    bool beginCoalescedPatternEdit(
        uint8_t step, core::state::sequencer::StepProperty property, uint32_t nowMs,
        core::state::sequencer::SequencerCoalescedPatternPayloadPlan payloadPlan,
        bool stateProperty = false) const;
    bool sealCoalescedPatternEdit(bool mutationChanged) const;
    bool beginCoalescedCcLaneEventEdit(uint8_t lane, uint8_t step, int32_t beforeValue,
                                       int32_t afterValue,
                                       const core::state::sequencer::SequencerCcLaneBank* afterBank,
                                       uint32_t nowMs) const;
    core::state::sequencer::SequencerPatternHistoryCommitOutcome commitCoalescedPatternEditOutcome()
        const;
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
private:
    SequencerHistoryDomainServices(void* context, const Operations* operations);
    static const Operations EMPTY_OPERATIONS;

    void* context_ = nullptr;
    const Operations* operations_ = &EMPTY_OPERATIONS;
};

static_assert(sizeof(SequencerHistoryDomainServices) == sizeof(void*) * 2U,
              "history domain facade must remain two pointers");

}  // namespace core::handler

#pragma once

#include "state/sequencer/SequencerHistory.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class SequencerHistoryDomainServices {
public:
    using RecordPatternFn = bool (*)(
        void* context,
        core::state::sequencer::SequencerHistoryPatternSnapshot before,
        core::state::sequencer::SequencerHistoryPatternSnapshot after,
        core::state::sequencer::SequencerHistoryDescriptor descriptor
    );
    using RecordPatternChangeFn = bool (*)(
        void* context,
        core::state::sequencer::SequencerHistoryPatternChangePtr change
    );
    using CanRecordPatternFn = bool (*)(
        void* context,
        const core::state::sequencer::SequencerHistoryPatternChange& change
    );
    using RecordPreparedPatternFn = void (*)(
        void* context,
        core::state::sequencer::SequencerHistoryPatternChangePtr change
    );
    using RecordFullBankFn = bool (*)(
        void* context,
        core::state::sequencer::SequencerHistoryFullBankChangePtr change
    );
    using CanRecordFullBankFn = bool (*)(
        void* context,
        const core::state::sequencer::SequencerHistoryFullBankChange& change
    );
    using RecordPreparedFullBankFn = void (*)(
        void* context,
        core::state::sequencer::SequencerHistoryFullBankChangePtr change
    );
    using RecordStructureFn = bool (*)(
        void* context,
        core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
    );
    using CanRecordStructureFn = bool (*)(
        void* context,
        const core::state::sequencer::SequencerHistoryTrackStructureChange& change
    );
    using RecordPreparedStructureFn = void (*)(
        void* context,
        core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
    );
    using CommandFn = bool (*)(void* context);
    using BeginCoalescedPatternEditFn = bool (*)(
        void* context,
        uint8_t step,
        core::state::sequencer::StepProperty property,
        uint32_t nowMs,
        bool stateProperty
    );
    using BeginCoalescedCcLaneEventEditFn = bool (*)(
        void* context,
        uint8_t lane,
        uint8_t step,
        int32_t beforeValue,
        int32_t afterValue,
        const core::state::sequencer::SequencerCcLaneBank* afterBank,
        uint32_t nowMs
    );

    struct Operations {
        void* context = nullptr;
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
        BeginCoalescedCcLaneEventEditFn beginCoalescedCcLaneEventEdit = nullptr;
        CommandFn commitCoalescedPatternEdit = nullptr;
    };

    SequencerHistoryDomainServices() = default;
    explicit SequencerHistoryDomainServices(Operations operations);
    static SequencerHistoryDomainServices fromCoreState(core::state::CoreState& state);

    bool recordPattern(
        core::state::sequencer::SequencerHistoryPatternSnapshot before,
        core::state::sequencer::SequencerHistoryPatternSnapshot after,
        core::state::sequencer::SequencerHistoryDescriptor descriptor = {}
    ) const;
    bool recordFlatPattern(
        core::state::sequencer::SequencerHistoryPatternSnapshot before,
        core::state::sequencer::SequencerHistoryPatternSnapshot after,
        core::state::sequencer::SequencerHistoryDescriptor descriptor = {}
    ) const;
    bool recordPattern(
        core::state::sequencer::SequencerHistoryPatternChangePtr change
    ) const;
    bool canRecordPattern(
        const core::state::sequencer::SequencerHistoryPatternChange& change
    ) const;
    // Precondition: canRecordPattern(change) was true and change is unchanged.
    void recordPreparedPattern(
        core::state::sequencer::SequencerHistoryPatternChangePtr change
    ) const;
    // Same admission contract, but the caller has already published an exact
    // editor-to-bank synchronization and may consume the generic coalescer.
    bool canRecordSynchronizedPattern(
        const core::state::sequencer::SequencerHistoryPatternChange& change
    ) const;
    void recordPreparedSynchronizedPattern(
        core::state::sequencer::SequencerHistoryPatternChangePtr change
    ) const;
    bool recordFullBank(
        core::state::sequencer::SequencerHistoryFullBankChangePtr change
    ) const;
    bool canRecordFullBank(
        const core::state::sequencer::SequencerHistoryFullBankChange& change
    ) const;
    // Precondition: canRecordFullBank(change) was true and change is unchanged.
    void recordPreparedFullBank(
        core::state::sequencer::SequencerHistoryFullBankChangePtr change
    ) const;
    bool canRecordStructure(
        const core::state::sequencer::SequencerHistoryTrackStructureChange& change
    ) const;
    // Precondition: canRecordStructure(change) was true and change is unchanged.
    void recordPreparedStructure(
        core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
    ) const;
    bool recordStructure(
        core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
    ) const;
    bool beginCoalescedPatternEdit(
        uint8_t step,
        core::state::sequencer::StepProperty property,
        uint32_t nowMs,
        bool stateProperty = false
    ) const;
    bool beginCoalescedCcLaneEventEdit(
        uint8_t lane,
        uint8_t step,
        int32_t beforeValue,
        int32_t afterValue,
        const core::state::sequencer::SequencerCcLaneBank* afterBank,
        uint32_t nowMs
    ) const;
    bool commitCoalescedPatternEdit() const;

private:
    Operations operations_{};
};

}  // namespace core::handler

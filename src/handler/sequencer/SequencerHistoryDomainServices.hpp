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
    using RecordFullBankFn = bool (*)(
        void* context,
        core::state::sequencer::SequencerHistoryFullBankChangePtr change
    );
    using CommandFn = bool (*)(void* context);
    using BeginCoalescedPatternEditFn = bool (*)(
        void* context,
        uint8_t step,
        core::state::sequencer::StepProperty property,
        uint32_t nowMs
    );

    struct Operations {
        void* context = nullptr;
        RecordPatternFn recordPattern = nullptr;
        RecordFullBankFn recordFullBank = nullptr;
        CommandFn undo = nullptr;
        CommandFn redo = nullptr;
        CommandFn clear = nullptr;
        BeginCoalescedPatternEditFn beginCoalescedPatternEdit = nullptr;
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
    bool recordFullBank(
        core::state::sequencer::SequencerHistoryFullBankChangePtr change
    ) const;
    bool undo() const;
    bool redo() const;
    bool clear() const;
    bool beginCoalescedPatternEdit(
        uint8_t step,
        core::state::sequencer::StepProperty property,
        uint32_t nowMs
    ) const;
    bool commitCoalescedPatternEdit() const;

private:
    Operations operations_{};
};

}  // namespace core::handler

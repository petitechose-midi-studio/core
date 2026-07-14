#include "handler/sequencer/SequencerHistoryDomainServices.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"

namespace core::handler {

namespace {

FLASHMEM bool recordPatternFromCoreState(
    void* context,
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor
) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->recordSequencerPatternHistory(
        std::move(before),
        std::move(after),
        descriptor
    );
}

FLASHMEM bool recordFlatPatternFromCoreState(
    void* context,
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor
) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->recordSequencerPatternHistory(
        std::move(before),
        std::move(after),
        descriptor,
        core::state::sequencer::SequencerHistoryPatternStorage::FlatOnly
    );
}

FLASHMEM bool recordPatternChangeFromCoreState(
    void* context,
    core::state::sequencer::SequencerHistoryPatternChangePtr change
) {
    if (context == nullptr) return false;
    return static_cast<core::state::CoreState*>(context)->recordSequencerPatternHistory(
        std::move(change)
    );
}

FLASHMEM bool canRecordPatternFromCoreState(
    void* context,
    const core::state::sequencer::SequencerHistoryPatternChange& change
) {
    if (context == nullptr) return false;
    return static_cast<const core::state::CoreState*>(context)
        ->sequencerHistory.canRecordPattern(change);
}

FLASHMEM void recordPreparedPatternFromCoreState(
    void* context,
    core::state::sequencer::SequencerHistoryPatternChangePtr change
) {
    if (context == nullptr || !change) return;
    auto* state = static_cast<core::state::CoreState*>(context);
    state->sequencerHistory.recordPreparedPattern(std::move(change));
    state->markProjectMutated();
}

FLASHMEM bool recordFullBankFromCoreState(
    void* context,
    core::state::sequencer::SequencerHistoryFullBankChangePtr change
) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->recordSequencerBankHistory(std::move(change));
}

FLASHMEM bool recordStructureFromCoreState(
    void* context,
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->recordSequencerStructureHistory(std::move(change));
}

FLASHMEM bool canRecordStructureFromCoreState(
    void* context,
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change
) {
    if (context == nullptr) {
        return false;
    }

    const auto* state = static_cast<const core::state::CoreState*>(context);
    return state->canRecordSequencerStructureHistory(change);
}

FLASHMEM void recordPreparedStructureFromCoreState(
    void* context,
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    if (context == nullptr) return;
    auto* state = static_cast<core::state::CoreState*>(context);
    state->recordPreparedSequencerStructureHistory(std::move(change));
}

FLASHMEM bool undoFromCoreState(void* context) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->undoSequencerHistory();
}

FLASHMEM bool redoFromCoreState(void* context) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->redoSequencerHistory();
}

FLASHMEM bool clearFromCoreState(void* context) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    state->clearSequencerHistory();
    return true;
}

FLASHMEM bool beginCoalescedPatternEditFromCoreState(
    void* context,
    uint8_t step,
    core::state::sequencer::StepProperty property,
    uint32_t nowMs
) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->beginOrContinueSequencerPatternHistoryCoalescing(step, property, nowMs);
}

FLASHMEM bool commitCoalescedPatternEditFromCoreState(void* context) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->commitSequencerPatternHistoryCoalescing();
}

}  // namespace

FLASHMEM SequencerHistoryDomainServices::SequencerHistoryDomainServices(Operations operations)
    : operations_(operations) {}

FLASHMEM SequencerHistoryDomainServices SequencerHistoryDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return SequencerHistoryDomainServices{
        Operations{
            .context = &state,
            .recordPattern = recordPatternFromCoreState,
            .recordFlatPattern = recordFlatPatternFromCoreState,
            .recordPatternChange = recordPatternChangeFromCoreState,
            .canRecordPattern = canRecordPatternFromCoreState,
            .recordPreparedPattern = recordPreparedPatternFromCoreState,
            .recordStructure = recordStructureFromCoreState,
            .canRecordStructure = canRecordStructureFromCoreState,
            .recordPreparedStructure = recordPreparedStructureFromCoreState,
            .recordFullBank = recordFullBankFromCoreState,
            .undo = undoFromCoreState,
            .redo = redoFromCoreState,
            .clear = clearFromCoreState,
            .beginCoalescedPatternEdit = beginCoalescedPatternEditFromCoreState,
            .commitCoalescedPatternEdit = commitCoalescedPatternEditFromCoreState,
        }
    };
}

FLASHMEM bool SequencerHistoryDomainServices::recordPattern(
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor
) const {
    return operations_.recordPattern != nullptr &&
           operations_.recordPattern(
               operations_.context,
               std::move(before),
               std::move(after),
               descriptor
           );
}

FLASHMEM bool SequencerHistoryDomainServices::canRecordPattern(
    const core::state::sequencer::SequencerHistoryPatternChange& change
) const {
    return operations_.canRecordPattern != nullptr &&
           operations_.recordPreparedPattern != nullptr &&
           operations_.canRecordPattern(operations_.context, change);
}

FLASHMEM void SequencerHistoryDomainServices::recordPreparedPattern(
    core::state::sequencer::SequencerHistoryPatternChangePtr change
) const {
    if (operations_.recordPreparedPattern == nullptr) return;
    operations_.recordPreparedPattern(operations_.context, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::recordFlatPattern(
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after,
    core::state::sequencer::SequencerHistoryDescriptor descriptor
) const {
    return operations_.recordFlatPattern != nullptr &&
           operations_.recordFlatPattern(
               operations_.context,
               std::move(before),
               std::move(after),
               descriptor
           );
}

FLASHMEM bool SequencerHistoryDomainServices::recordPattern(
    core::state::sequencer::SequencerHistoryPatternChangePtr change
) const {
    return operations_.recordPatternChange != nullptr &&
           operations_.recordPatternChange(operations_.context, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::recordStructure(
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
) const {
    return operations_.recordStructure != nullptr &&
           operations_.recordStructure(
               operations_.context,
               std::move(change)
           );
}

FLASHMEM bool SequencerHistoryDomainServices::canRecordStructure(
    const core::state::sequencer::SequencerHistoryTrackStructureChange& change
) const {
    return operations_.canRecordStructure != nullptr &&
           operations_.recordPreparedStructure != nullptr &&
           operations_.canRecordStructure(operations_.context, change);
}

FLASHMEM void SequencerHistoryDomainServices::recordPreparedStructure(
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
) const {
    if (operations_.recordPreparedStructure == nullptr) return;
    operations_.recordPreparedStructure(operations_.context, std::move(change));
}

FLASHMEM bool SequencerHistoryDomainServices::recordFullBank(
    core::state::sequencer::SequencerHistoryFullBankChangePtr change
) const {
    return operations_.recordFullBank != nullptr &&
           operations_.recordFullBank(
               operations_.context,
               std::move(change)
           );
}

FLASHMEM bool SequencerHistoryDomainServices::undo() const {
    return operations_.undo != nullptr && operations_.undo(operations_.context);
}

FLASHMEM bool SequencerHistoryDomainServices::redo() const {
    return operations_.redo != nullptr && operations_.redo(operations_.context);
}

FLASHMEM bool SequencerHistoryDomainServices::clear() const {
    return operations_.clear != nullptr && operations_.clear(operations_.context);
}

FLASHMEM bool SequencerHistoryDomainServices::beginCoalescedPatternEdit(
    uint8_t step,
    core::state::sequencer::StepProperty property,
    uint32_t nowMs
) const {
    return operations_.beginCoalescedPatternEdit != nullptr &&
           operations_.beginCoalescedPatternEdit(
               operations_.context,
               step,
               property,
               nowMs
           );
}

FLASHMEM bool SequencerHistoryDomainServices::commitCoalescedPatternEdit() const {
    return operations_.commitCoalescedPatternEdit != nullptr &&
           operations_.commitCoalescedPatternEdit(operations_.context);
}

}  // namespace core::handler

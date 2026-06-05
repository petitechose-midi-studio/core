#include "handler/sequencer/SequencerHistoryDomainServices.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"

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
            &state,
            recordPatternFromCoreState,
            undoFromCoreState,
            redoFromCoreState,
            beginCoalescedPatternEditFromCoreState,
            commitCoalescedPatternEditFromCoreState,
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

FLASHMEM bool SequencerHistoryDomainServices::undo() const {
    return operations_.undo != nullptr && operations_.undo(operations_.context);
}

FLASHMEM bool SequencerHistoryDomainServices::redo() const {
    return operations_.redo != nullptr && operations_.redo(operations_.context);
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

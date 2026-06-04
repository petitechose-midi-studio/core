#include "handler/sequencer/SequencerHistoryDomainServices.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

namespace {

FLASHMEM bool recordPatternFromCoreState(
    void* context,
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after
) {
    if (context == nullptr) {
        return false;
    }

    auto* state = static_cast<core::state::CoreState*>(context);
    return state->recordSequencerPatternHistory(std::move(before), std::move(after));
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
        }
    };
}

FLASHMEM bool SequencerHistoryDomainServices::recordPattern(
    core::state::sequencer::SequencerHistoryPatternSnapshot before,
    core::state::sequencer::SequencerHistoryPatternSnapshot after
) const {
    return operations_.recordPattern != nullptr &&
           operations_.recordPattern(
               operations_.context,
               std::move(before),
               std::move(after)
           );
}

FLASHMEM bool SequencerHistoryDomainServices::undo() const {
    return operations_.undo != nullptr && operations_.undo(operations_.context);
}

FLASHMEM bool SequencerHistoryDomainServices::redo() const {
    return operations_.redo != nullptr && operations_.redo(operations_.context);
}

}  // namespace core::handler

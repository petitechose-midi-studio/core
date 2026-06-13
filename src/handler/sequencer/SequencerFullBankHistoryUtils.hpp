#pragma once

#include <utility>

#include "app/ExtmemAllocator.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/sequencer/SequencerHistory.hpp"

namespace core::handler {

inline core::state::sequencer::SequencerHistoryFullBankChangePtr
captureSequencerFullBankHistoryBefore(
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::sequencer::SequencerState& sequencer
) {
    auto change = core::app::makeExtmemUnique<
        core::state::sequencer::SequencerHistoryFullBankChange
    >();
    if (!change) return nullptr;

    if (!core::state::sequencer::captureHistorySnapshot(
            tracks,
            sequencer,
            change->before
        )) {
        return nullptr;
    }

    return change;
}

inline bool captureSequencerFullBankHistoryAfter(
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerHistoryFullBankChange& change
) {
    return core::state::sequencer::captureHistorySnapshot(
        tracks,
        sequencer,
        change.after
    );
}

inline bool recordSequencerFullBankHistoryChange(
    SequencerHistoryDomainServices& history,
    core::state::sequencer::SequencerHistoryFullBankChangePtr change,
    core::state::sequencer::SequencerHistoryDescriptor descriptor
) {
    if (!change) return false;
    change->descriptor = descriptor;
    return history.recordFullBank(std::move(change));
}

}  // namespace core::handler

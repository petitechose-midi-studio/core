#pragma once

#include <utility>

#include "app/ExtmemAllocator.hpp"
#include "handler/sequencer/SequencerHistoryDomainServices.hpp"
#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerStructureHistory.hpp"

namespace core::handler {

inline uint16_t sequencerStructureHistoryTrackBit(uint8_t trackIndex) {
    return core::state::sequencer::sequencerHistoryTrackBit(trackIndex);
}

core::state::sequencer::SequencerHistoryTrackStructureChangePtr
captureSequencerTrackStructureHistoryBefore(
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t trackMask
);

bool captureSequencerTrackStructureHistoryAfter(
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t trackMask,
    core::state::sequencer::SequencerHistoryTrackStructureChange& change
);

inline core::state::sequencer::SequencerHistoryDescriptor makeSequencerTrackStructureHistoryDescriptor(
    const core::state::sequencer::SequencerHistoryTrackStructureSnapshot& before,
    const core::state::sequencer::SequencerHistoryTrackStructureSnapshot& after
) {
    const int32_t beforeValue =
        core::state::sequencer::sequencerHistoryEnabledTrackCount(before.enabledMask);
    const int32_t afterValue =
        core::state::sequencer::sequencerHistoryEnabledTrackCount(after.enabledMask);

    return core::state::sequencer::SequencerHistoryDescriptor{
        .kind = core::state::sequencer::SequencerHistoryActionKind::TrackStructure,
        .trackIndex = core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            after.activeTrack
        ),
        .hasValue = beforeValue != afterValue,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
}

inline bool recordSequencerTrackStructureHistoryChange(
    SequencerHistoryDomainServices& history,
    core::state::sequencer::SequencerHistoryTrackStructureChangePtr change
) {
    if (!change) return false;
    change->descriptor = makeSequencerTrackStructureHistoryDescriptor(
        change->before,
        change->after
    );
    return history.recordStructure(std::move(change));
}

}  // namespace core::handler

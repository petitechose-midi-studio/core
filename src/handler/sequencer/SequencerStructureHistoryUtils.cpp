#include "handler/sequencer/SequencerStructureHistoryUtils.hpp"

#include <config/PlatformCompat.hpp>

namespace core::handler {

FLASHMEM core::state::sequencer::SequencerHistoryTrackStructureChangePtr
captureSequencerTrackStructureHistoryBefore(
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t trackMask
) {
    auto change = core::app::makeExtmemUnique<
        core::state::sequencer::SequencerHistoryTrackStructureChange
    >();
    if (!change) return nullptr;

    if (!core::state::sequencer::captureHistoryStructureSnapshot(
            tracks,
            sequencer,
            trackMask,
            change->before
        )) {
        return nullptr;
    }

    for (uint8_t i = 0;
         i < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
         ++i) {
        if ((change->before.capturedTrackMask & sequencerStructureHistoryTrackBit(i)) == 0) {
            continue;
        }
        if (!core::state::sequencer::reserveHistorySnapshotGraphStorage(
                change->after.tracks[i]
            )) {
            return nullptr;
        }
    }

    return change;
}

FLASHMEM bool captureSequencerTrackStructureHistoryAfter(
    const core::state::sequencer::SequencerTrackBankState& tracks,
    const core::state::sequencer::SequencerState& sequencer,
    uint16_t trackMask,
    core::state::sequencer::SequencerHistoryTrackStructureChange& change
) {
    return core::state::sequencer::captureHistoryStructureSnapshotUsingReservedGraphs(
        tracks,
        sequencer,
        trackMask,
        change.after
    );
}

}  // namespace core::handler

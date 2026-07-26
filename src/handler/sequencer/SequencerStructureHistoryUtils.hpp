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

using SequencerPageStructureHistoryChangePtr =
    core::state::sequencer::SequencerHistoryPatternChangePtr;

inline uint8_t sequencerHistoryPageCount(
    const core::state::sequencer::SequencerHistoryPatternSnapshot& snapshot
) {
    const uint8_t length = snapshot.flat.length;
    if (length == 0) return 0;
    const uint8_t pages = static_cast<uint8_t>(
        (length + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1U) /
        core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    return pages > core::state::sequencer::SequencerState::PAGE_COUNT
        ? core::state::sequencer::SequencerState::PAGE_COUNT
        : pages;
}

inline core::state::sequencer::SequencerHistoryDescriptor makeSequencerPageStructureHistoryDescriptor(
    const core::state::sequencer::SequencerHistoryPatternSnapshot& before,
    const core::state::sequencer::SequencerHistoryPatternSnapshot& after,
    uint8_t trackIndex
) {
    const int32_t beforeValue = sequencerHistoryPageCount(before);
    const int32_t afterValue = sequencerHistoryPageCount(after);

    return core::state::sequencer::SequencerHistoryDescriptor{
        .kind = core::state::sequencer::SequencerHistoryActionKind::PageStructure,
        .trackIndex = core::state::sequencer::SequencerTrackBankState::clampTrackIndex(trackIndex),
        .hasValue = beforeValue != afterValue,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
}

SequencerPageStructureHistoryChangePtr captureSequencerPageStructureHistoryBefore(
    const core::state::sequencer::SequencerState& sequencer
);

inline bool recordSequencerPageStructureHistoryChange(
    SequencerHistoryDomainServices& history,
    const core::state::sequencer::SequencerState& sequencer,
    SequencerPageStructureHistoryChangePtr change,
    uint8_t trackIndex
) {
    if (!change ||
        !core::state::sequencer::captureHistorySnapshotUsingReservedGraph(
            sequencer,
            change->after
        )) {
        return false;
    }

    change->trackIndex = core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
        trackIndex
    );
    change->descriptor = makeSequencerPageStructureHistoryDescriptor(
        change->before,
        change->after,
        change->trackIndex
    );
    return history.recordPattern(std::move(change));
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

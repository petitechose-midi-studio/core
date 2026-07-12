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

inline SequencerPageStructureHistoryChangePtr captureSequencerPageStructureHistoryBefore(
    const core::state::sequencer::SequencerState& sequencer
) {
    auto change = core::app::makeExtmemUnique<
        core::state::sequencer::SequencerHistoryPatternChange
    >();
    if (!change ||
        !core::state::sequencer::captureHistorySnapshot(sequencer, change->before) ||
        !core::state::sequencer::reserveHistorySnapshotGraphStorage(change->after)) {
        return nullptr;
    }
    change->storage = core::state::sequencer::SequencerHistoryPatternStorage::FullGraph;
    return change;
}

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

inline core::state::sequencer::SequencerHistoryTrackStructureChangePtr
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

inline bool captureSequencerTrackStructureHistoryAfter(
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

#pragma once

#include "state/sequencer/SequencerHistory.hpp"

namespace core::handler {

inline uint8_t sequencerHistoryTrackCount(
    const core::state::sequencer::SequencerHistoryTrackBankSnapshot& snapshot
) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT; ++i) {
        const uint16_t bit = static_cast<uint16_t>(1U << i);
        if ((snapshot.flat.enabledMask & bit) != 0) {
            ++count;
        }
    }
    return count;
}

inline uint8_t sequencerHistoryPageCount(
    const core::state::sequencer::SequencerHistoryTrackBankSnapshot& snapshot
) {
    const uint8_t activeTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            snapshot.flat.activeTrack
        );
    const uint8_t length = snapshot.flat.tracks[activeTrack].length;
    if (length == 0) return 0;
    const uint8_t pages = static_cast<uint8_t>(
        (length + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1U) /
        core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );
    return pages > core::state::sequencer::SequencerState::PAGE_COUNT
        ? core::state::sequencer::SequencerState::PAGE_COUNT
        : pages;
}

inline core::state::sequencer::SequencerHistoryDescriptor makeSequencerStructureHistoryDescriptor(
    core::state::sequencer::SequencerHistoryActionKind kind,
    const core::state::sequencer::SequencerHistoryTrackBankSnapshot& before,
    const core::state::sequencer::SequencerHistoryTrackBankSnapshot& after
) {
    int32_t beforeValue = 0;
    int32_t afterValue = 0;

    if (kind == core::state::sequencer::SequencerHistoryActionKind::TrackStructure) {
        beforeValue = sequencerHistoryTrackCount(before);
        afterValue = sequencerHistoryTrackCount(after);
    } else if (kind == core::state::sequencer::SequencerHistoryActionKind::PageStructure) {
        beforeValue = sequencerHistoryPageCount(before);
        afterValue = sequencerHistoryPageCount(after);
    }

    return core::state::sequencer::SequencerHistoryDescriptor{
        .kind = kind,
        .trackIndex = core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            after.flat.activeTrack
        ),
        .hasValue = beforeValue != afterValue,
        .beforeValue = beforeValue,
        .afterValue = afterValue,
    };
}

}  // namespace core::handler

#pragma once

#include <array>
#include <cstdint>

#include "state/sequencer/SequencerHistory.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"

namespace core::state::sequencer {

struct SequencerHistoryTrackStructureSnapshot {
    uint16_t enabledMask = 0x0001;
    uint16_t mutedMask = 0;
    uint8_t activeTrack = 0;
    uint8_t focusedStep = 0;
    uint8_t page = 0;
    uint16_t capturedTrackMask = 0x0001;
    std::array<SequencerHistoryPatternSnapshot, SequencerTrackBankState::TRACK_COUNT> tracks{};

    SequencerHistoryTrackStructureSnapshot();
    ~SequencerHistoryTrackStructureSnapshot();
    SequencerHistoryTrackStructureSnapshot(const SequencerHistoryTrackStructureSnapshot&) = delete;
    SequencerHistoryTrackStructureSnapshot& operator=(
        const SequencerHistoryTrackStructureSnapshot&
    ) = delete;
    SequencerHistoryTrackStructureSnapshot(SequencerHistoryTrackStructureSnapshot&&) noexcept;
    SequencerHistoryTrackStructureSnapshot& operator=(
        SequencerHistoryTrackStructureSnapshot&&
    ) noexcept;
};

struct SequencerHistoryTrackStructureChange {
    SequencerHistoryDescriptor descriptor{};
    // Tracks whose destination-owned bindings must survive history traversal.
    // Track paste sets this for its destinations so Undo/Redo restores musical
    // content without rolling routing back to the captured snapshot.
    uint16_t preserveDestinationBindingsMask = 0;
    // Stable Track paste operation identity. Each audible Undo/Redo transition
    // receives a fresh generation, while stacked operations may safely rebind
    // the single realtime slot for a Track.
    SequencerTrackActivationHistoryRef activation{};
    SequencerHistoryTrackStructureSnapshot before;
    SequencerHistoryTrackStructureSnapshot after;

    SequencerHistoryTrackStructureChange();
    ~SequencerHistoryTrackStructureChange();
    SequencerHistoryTrackStructureChange(const SequencerHistoryTrackStructureChange&) = delete;
    SequencerHistoryTrackStructureChange& operator=(
        const SequencerHistoryTrackStructureChange&
    ) = delete;
    SequencerHistoryTrackStructureChange(SequencerHistoryTrackStructureChange&&) noexcept;
    SequencerHistoryTrackStructureChange& operator=(
        SequencerHistoryTrackStructureChange&&
    ) noexcept;
};

uint16_t sequencerHistoryTrackBit(uint8_t trackIndex);
uint16_t sequencerHistorySanitizeTrackMask(uint16_t trackMask);
uint8_t sequencerHistoryEnabledTrackCount(uint16_t enabledMask);

bool captureHistoryStructureSnapshot(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
);
bool captureHistoryStructureSnapshotUsingReservedGraphs(
    const SequencerTrackBankState& bank,
    const SequencerState& active,
    uint16_t trackMask,
    SequencerHistoryTrackStructureSnapshot& out
);

bool applyHistoryStructureSnapshot(
    SequencerTrackBankState& bank,
    SequencerState& active,
    const SequencerHistoryTrackStructureSnapshot& snapshot,
    uint16_t preserveDestinationBindingsMask
);

bool sameMusicalHistoryStructureSnapshot(
    const SequencerHistoryTrackStructureSnapshot& lhs,
    const SequencerHistoryTrackStructureSnapshot& rhs
);

}  // namespace core::state::sequencer

#pragma once

#include <array>
#include <cstdint>

#include "sequencer/SequencerRuntimeStateSync.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::sequencer {

/**
 * Double-buffered sequencer snapshot bridge for runtime lanes.
 *
 * Loop code refreshes the inactive snapshot from editor/track-bank state, then
 * commits it under an interrupt guard. Timer/playback code reads only the active
 * snapshot, which keeps realtime lanes away from mutable editor state.
 */
class SequencerRuntimeSnapshotBank {
public:
    using Snapshot = core::state::sequencer::SequencerTrackBankSnapshot;

    SequencerRuntimeSnapshotBank(core::state::sequencer::SequencerState& sequencer,
                                 core::state::sequencer::SequencerTrackBankState& trackBank);

    uint8_t refresh();
    void commit(uint8_t snapshotIndex);

    const Snapshot& snapshot(uint8_t snapshotIndex) const;
    const Snapshot& activeSnapshot() const;
    uint8_t activeIndex() const { return active_index_; }

private:
    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& track_bank_;
    std::array<Snapshot, 2> snapshots_{};
    std::array<SequencerRuntimeStateSignature, core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>
        track_signatures_{};
    uint32_t project_scale_revision_ = UINT32_MAX;
    volatile uint8_t active_index_ = 0;
};

}  // namespace core::sequencer

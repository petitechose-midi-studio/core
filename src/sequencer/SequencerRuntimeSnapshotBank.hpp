#pragma once

#include <array>
#include <cstdint>

#include "sequencer/SequencerRuntimeStateSync.hpp"
#include "state/project/ProjectNavigationState.hpp"
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
                                 core::state::sequencer::SequencerTrackBankState& trackBank,
                                 core::state::project::ProjectNavigationState& projectNavigation);

    uint8_t refresh();
    void commit(uint8_t snapshotIndex);

    const Snapshot& snapshot(uint8_t snapshotIndex) const;
    const Snapshot& activeSnapshot() const;
    uint8_t activeIndex() const { return active_index_; }

private:
    using TrackSignatures = std::array<
        SequencerRuntimeStateSignature,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>;

    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& track_bank_;
    core::state::project::ProjectNavigationState& project_navigation_;
    std::array<Snapshot, 2> snapshots_{};
    // Each double-buffer slot can lag independently; signatures are per slot.
    std::array<TrackSignatures, 2> track_signatures_{};
    volatile uint8_t active_index_ = 0;
};

}  // namespace core::sequencer

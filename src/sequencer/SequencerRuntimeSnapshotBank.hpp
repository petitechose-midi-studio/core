#pragma once

#include <array>
#include <cstdint>

#include "app/ExtmemAllocator.hpp"
#include "sequencer/SequencerRuntimeStateSync.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerSnapshots.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"
#include "state/sequencer/DrumPatternState.hpp"

namespace core::sequencer {

/**
 * Immutable Pattern-owned CC payload published with one flat runtime snapshot.
 * The two frames are allocated lazily in EXTMEM only after a Project contains
 * at least one lane; empty projects retain only two pointers in RAM2.
 */
struct SequencerCcLaneRuntimeProjectSnapshot {
    static constexpr uint8_t TRACK_COUNT =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    uint16_t presentMask = 0;
    std::array<
        core::state::sequencer::SequencerCcLaneBank,
        TRACK_COUNT
    > tracks{};

    [[nodiscard]] const core::state::sequencer::SequencerCcLaneBank* lanesForTrack(
        uint8_t track
    ) const {
        if (track >= TRACK_COUNT ||
            (presentMask & static_cast<uint16_t>(1U << track)) == 0) {
            return nullptr;
        }
        return &tracks[track];
    }
};

/**
 * Immutable Drum payload paired with one flat Track-bank publication.
 *
 * Frames are allocated in PSRAM only after a Project contains a Drum Track.
 * Every Track retains an independent payload and phase; `presentMask` is the
 * runtime Track-kind authority for selecting Drum versus melodic playback.
 */
struct SequencerDrumRuntimeProjectSnapshot {
    static constexpr uint8_t TRACK_COUNT =
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;

    uint16_t presentMask = 0U;
    std::array<uint32_t, TRACK_COUNT> sourceRevisions{};
    std::array<
        core::state::sequencer::DrumPatternRuntimeSnapshot,
        TRACK_COUNT
    > tracks{};

    [[nodiscard]] const core::state::sequencer::DrumPatternRuntimeSnapshot*
        patternForTrack(uint8_t track) const {
        if (track >= TRACK_COUNT ||
            (presentMask & static_cast<uint16_t>(1U << track)) == 0U) {
            return nullptr;
        }
        return &tracks[track];
    }
};

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
    const SequencerCcLaneRuntimeProjectSnapshot* laneSnapshot(
        uint8_t snapshotIndex
    ) const;
    const SequencerDrumRuntimeProjectSnapshot* drumSnapshot(
        uint8_t snapshotIndex
    ) const;
    [[nodiscard]] bool lastRefreshSucceeded() const {
        return last_refresh_succeeded_;
    }
    [[nodiscard]] uint32_t lanePayloadWriteCount() const {
        return lane_payload_write_count_;
    }
    const Snapshot& activeSnapshot() const;
    uint8_t activeIndex() const { return active_index_; }

private:
    using TrackSignatures = std::array<
        SequencerRuntimeStateSignature,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>;

    struct LaneSourceSignature {
        const core::state::sequencer::SequencerCcLaneBank* identity = nullptr;
        uint32_t revision = 0;

        [[nodiscard]] bool matches(
            const core::state::sequencer::SequencerCcLaneBank* source,
            uint32_t sourceRevision
        ) const {
            if (identity == nullptr && source == nullptr) return true;
            return identity == source && revision == sourceRevision;
        }
    };
    using LaneSourceSignatures = std::array<
        LaneSourceSignature,
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT>;

    core::state::sequencer::SequencerState& sequencer_;
    core::state::sequencer::SequencerTrackBankState& track_bank_;
    core::state::project::ProjectNavigationState& project_navigation_;
    std::array<Snapshot, 2> snapshots_{};
    // Each double-buffer slot can lag independently; signatures are per slot.
    std::array<TrackSignatures, 2> track_signatures_{};
    std::array<LaneSourceSignatures, 2> lane_source_signatures_{};
    std::array<
        core::app::ExtmemUniquePtr<SequencerCcLaneRuntimeProjectSnapshot>,
        2
    > lane_snapshots_{};
    bool refreshDrumTracks_(uint8_t writeIndex);
    std::array<
        core::app::ExtmemUniquePtr<SequencerDrumRuntimeProjectSnapshot>,
        2
    > drum_snapshots_{};
    volatile uint8_t active_index_ = 0;
    uint32_t lane_payload_write_count_ = 0;
    bool last_refresh_succeeded_ = true;
};

static_assert(sizeof(SequencerCcLaneRuntimeProjectSnapshot) <= 16U * 1024U);
static_assert(sizeof(SequencerDrumRuntimeProjectSnapshot) <= 192U * 1024U);

}  // namespace core::sequencer

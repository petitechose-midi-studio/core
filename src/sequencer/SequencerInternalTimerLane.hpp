#pragma once

#include <array>
#include <cstdint>

#include <oc/api/MidiAPI.hpp>
#include <oc/realtime/PeriodicTimer.hpp>

#include "sequencer/InternalTransportClock.hpp"
#include "sequencer/MidiClockSyncService.hpp"
#include "sequencer/ProjectTrackRuntimeSnapshotBank.hpp"
#include "sequencer/RealtimeMidiQueue.hpp"
#include "sequencer/SequencerPlaybackService.hpp"
#include "sequencer/SequencerRuntimeSnapshotBank.hpp"

namespace core::sequencer {

/**
 * Internal transport timer lane for the standalone sequencer.
 *
 * This lane owns the high-frequency timer callback path: it reads the committed
 * runtime snapshot, advances internal transport clock output, updates playback,
 * and drains due realtime MIDI. Loop/context code publishes inputs through
 * `publishRealtimeInputs`; it should not mutate the timer callback state directly.
 */
class SequencerInternalTimerLane {
public:
    SequencerInternalTimerLane(oc::api::MidiAPI& midi,
                               RealtimeMidiQueue& midiQueue,
                               SequencerRuntimeSnapshotBank& snapshotBank,
                               const ProjectTrackRuntimeSnapshotBank& projectTrackSnapshots,
                               SequencerPlaybackService& playback);

    bool start();
    void stop();
    void publishRealtimeInputs(const MidiClockSyncRuntimeConfig& config, uint8_t snapshotIndex);

private:
    void onTimer_();
    void drainRealtimeMidiQueue_(uint32_t nowUs);

    oc::api::MidiAPI& midi_;
    RealtimeMidiQueue& midi_queue_;
    SequencerRuntimeSnapshotBank& snapshot_bank_;
    const ProjectTrackRuntimeSnapshotBank& project_track_snapshots_;
    SequencerPlaybackService& playback_;
    oc::realtime::PeriodicTimer timer_{};
    InternalTransportClock clock_{};
    std::array<MidiClockSyncRuntimeConfig, 2> configs_{};
    bool running_ = false;
    bool playing_ = false;
    uint32_t last_tick_sent_ = 0;
};

}  // namespace core::sequencer

#pragma once

#include <array>
#include <cstdint>

#include <oc/api/MidiAPI.hpp>
#include <oc/realtime/PeriodicTimer.hpp>

#include "sequencer/InternalTransportClock.hpp"
#include "sequencer/MidiClockSyncService.hpp"
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
                               SequencerPlaybackService& playback);

    bool start();
    void stop();
    void publishRealtimeInputs(const MidiClockSyncRuntimeConfig& config, uint8_t snapshotIndex);

private:
    static constexpr uint32_t TIMER_PERIOD_US = 1000;

    void onTimer_();
    void drainRealtimeMidiQueue_(uint32_t nowUs);

    oc::api::MidiAPI& midi_;
    RealtimeMidiQueue& midi_queue_;
    SequencerRuntimeSnapshotBank& snapshot_bank_;
    SequencerPlaybackService& playback_;
    oc::realtime::PeriodicTimer timer_{};
    InternalTransportClock clock_{};
    std::array<MidiClockSyncRuntimeConfig, 2> configs_{};
    bool running_ = false;
    bool playing_ = false;
    uint32_t last_tick_sent_ = 0;
};

}  // namespace core::sequencer

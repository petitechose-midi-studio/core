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
 * drains due realtime MIDI, and records timer-lane profiling. Loop/context code
 * publishes inputs through `publishRealtimeInputs`; it should not mutate the
 * timer callback state directly.
 */
class SequencerInternalTimerLane {
public:
    struct ProfilingSnapshot {
        uint32_t callbackCount = 0;
        uint32_t totalCallbackUs = 0;
        uint32_t maxCallbackUs = 0;
        uint32_t totalPlaybackUs = 0;
        uint32_t maxPlaybackUs = 0;
        uint32_t totalBackendDrainUs = 0;
        uint32_t maxBackendDrainUs = 0;
        uint32_t maxClockBurst = 0;
        uint32_t clockBurstClampCount = 0;
    };

    SequencerInternalTimerLane(oc::api::MidiAPI& midi,
                               RealtimeMidiQueue& midiQueue,
                               SequencerRuntimeSnapshotBank& snapshotBank,
                               SequencerPlaybackService& playback);

    bool start();
    void stop();
    void publishRealtimeInputs(const MidiClockSyncRuntimeConfig& config, uint8_t snapshotIndex);
    void resetProfiling(uint32_t nowMs);
    ProfilingSnapshot takeProfiling(uint32_t nowMs);

private:
    static constexpr uint32_t TIMER_PERIOD_US = 1000;

    struct ProfilingWindow {
        uint32_t windowStartMs = 0;
        ProfilingSnapshot snapshot{};

        void reset(uint32_t nowMs) {
            windowStartMs = nowMs;
            snapshot = {};
        }
    };

    void onTimer_();
    void drainRealtimeMidiQueue_(uint32_t nowUs);
    void recordProfiling_(uint32_t callbackUs,
                          uint32_t playbackUs,
                          uint32_t backendDrainUs,
                          uint32_t clockBurst,
                          bool clockBurstClamped,
                          uint32_t nowMs);

    oc::api::MidiAPI& midi_;
    RealtimeMidiQueue& midi_queue_;
    SequencerRuntimeSnapshotBank& snapshot_bank_;
    SequencerPlaybackService& playback_;
    oc::realtime::PeriodicTimer timer_{};
    InternalTransportClock clock_{};
    std::array<MidiClockSyncRuntimeConfig, 2> configs_{};
    ProfilingWindow profiling_{};
    bool running_ = false;
    bool playing_ = false;
    uint32_t last_tick_sent_ = 0;
};

}  // namespace core::sequencer

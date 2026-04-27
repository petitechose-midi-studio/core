#include "sequencer/SequencerInternalTimerLane.hpp"

#include <algorithm>

#include <oc/realtime/InterruptGuard.hpp>

#include "config/TimeCompat.hpp"
#include "sequencer/SequencerTiming.hpp"

namespace core::sequencer {

SequencerInternalTimerLane::SequencerInternalTimerLane(
    oc::api::MidiAPI& midi,
    RealtimeMidiQueue& midiQueue,
    SequencerRuntimeSnapshotBank& snapshotBank,
    SequencerPlaybackService& playback
)
    : midi_(midi)
    , midi_queue_(midiQueue)
    , snapshot_bank_(snapshotBank)
    , playback_(playback) {}

bool SequencerInternalTimerLane::start() {
    if (running_) {
        return true;
    }

    clock_.reset();
    playing_ = false;
    last_tick_sent_ = 0;
    timer_.setPriority(128);
    running_ = timer_.begin([this]() { onTimer_(); }, TIMER_PERIOD_US);
    return running_;
}

void SequencerInternalTimerLane::stop() {
    if (running_) {
        timer_.end();
        running_ = false;
    }

    clock_.reset();
    playing_ = false;
    last_tick_sent_ = 0;
}

void SequencerInternalTimerLane::publishRealtimeInputs(const MidiClockSyncRuntimeConfig& config,
                                                       uint8_t snapshotIndex) {
    configs_[snapshotIndex] = config;
    snapshot_bank_.commit(snapshotIndex);
}

void SequencerInternalTimerLane::resetProfiling(uint32_t nowMs) {
    oc::realtime::InterruptGuard lock;
    profiling_.reset(nowMs);
}

SequencerInternalTimerLane::ProfilingSnapshot SequencerInternalTimerLane::takeProfiling(
    uint32_t nowMs
) {
    oc::realtime::InterruptGuard lock;
    const auto snapshot = profiling_.snapshot;
    profiling_.reset(nowMs);
    return snapshot;
}

void SequencerInternalTimerLane::onTimer_() {
    const uint32_t timerStartUs = core::time_compat::micros();
    const uint8_t inputIndex = snapshot_bank_.activeIndex();
    const auto& snapshot = snapshot_bank_.activeSnapshot();
    const auto config = configs_[inputIndex];

    clock_.setBpm(config.tempo);
    clock_.setPlaying(config.playing);

    const bool playing = clock_.isPlaying();
    const uint32_t tick = clock_.tick();

    if (playing && !playing_) {
        midi_.sendStart();
    } else if (!playing && playing_) {
        midi_.sendStop();
    }

    playing_ = playing;

    uint32_t pendingClockCount = 0;
    bool clockBurstClamped = false;

    if (!playing) {
        last_tick_sent_ = tick;
    } else {
        if (tick < last_tick_sent_) {
            last_tick_sent_ = tick;
        }

        pendingClockCount = tick - last_tick_sent_;
        if (pendingClockCount > MAX_REALTIME_CLOCK_BURST_PER_UPDATE) {
            pendingClockCount = MAX_REALTIME_CLOCK_BURST_PER_UPDATE;
            clockBurstClamped = true;
        }

        for (uint32_t i = 0; i < pendingClockCount; ++i) {
            midi_.sendClock();
            last_tick_sent_ += 1U;
        }
    }

    const uint32_t nowMs = core::time_compat::millis();
    const uint32_t playbackStartUs = core::time_compat::micros();
    playback_.update(snapshot,
                     tick,
                     playing,
                     nowMs,
                     playbackStartUs,
                     tickPeriodUsForTempo(config.tempo),
                     false,
                     false);
    const uint32_t playbackUs = core::time_compat::micros() - playbackStartUs;
    const uint32_t drainStartUs = core::time_compat::micros();
    drainRealtimeMidiQueue_(drainStartUs);
    const uint32_t backendDrainUs = core::time_compat::micros() - drainStartUs;
    const uint32_t callbackUs = core::time_compat::micros() - timerStartUs;

    recordProfiling_(callbackUs,
                     playbackUs,
                     backendDrainUs,
                     pendingClockCount,
                     clockBurstClamped,
                     nowMs);
}

void SequencerInternalTimerLane::drainRealtimeMidiQueue_(uint32_t nowUs) {
    midi_queue_.drainDue(midi_, nowUs);
    midi_.serviceOutput(RealtimeMidiQueue::MAX_DRAIN_BUDGET_US);
}

void SequencerInternalTimerLane::recordProfiling_(uint32_t callbackUs,
                                                  uint32_t playbackUs,
                                                  uint32_t backendDrainUs,
                                                  uint32_t clockBurst,
                                                  bool clockBurstClamped,
                                                  uint32_t nowMs) {
    if (profiling_.windowStartMs == 0) {
        profiling_.reset(nowMs);
    }

    auto& snapshot = profiling_.snapshot;
    snapshot.callbackCount += 1;
    snapshot.totalCallbackUs += callbackUs;
    snapshot.maxCallbackUs = std::max(snapshot.maxCallbackUs, callbackUs);
    snapshot.totalPlaybackUs += playbackUs;
    snapshot.maxPlaybackUs = std::max(snapshot.maxPlaybackUs, playbackUs);
    snapshot.totalBackendDrainUs += backendDrainUs;
    snapshot.maxBackendDrainUs = std::max(snapshot.maxBackendDrainUs, backendDrainUs);
    snapshot.maxClockBurst = std::max(snapshot.maxClockBurst, clockBurst);
    if (clockBurstClamped) {
        snapshot.clockBurstClampCount += 1;
    }
}

}  // namespace core::sequencer

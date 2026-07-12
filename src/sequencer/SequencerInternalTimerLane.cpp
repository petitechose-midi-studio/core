#include "sequencer/SequencerInternalTimerLane.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

#include "config/TimeCompat.hpp"
#include "sequencer/SequencerTiming.hpp"

namespace core::sequencer {

FLASHMEM SequencerInternalTimerLane::SequencerInternalTimerLane(
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

void SequencerInternalTimerLane::onTimer_() {
    OC_PERF_SCOPE(perfTimer, "sequencer.timer");
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

    if (!playing) {
        last_tick_sent_ = tick;
    } else {
        if (tick < last_tick_sent_) {
            last_tick_sent_ = tick;
        }

        pendingClockCount = tick - last_tick_sent_;
        if (pendingClockCount > MAX_REALTIME_CLOCK_BURST_PER_UPDATE) {
            pendingClockCount = MAX_REALTIME_CLOCK_BURST_PER_UPDATE;
        }

        for (uint32_t i = 0; i < pendingClockCount; ++i) {
            midi_.sendClock();
            last_tick_sent_ += 1U;
        }
    }

    const uint32_t playbackStartUs = core::time_compat::micros();
    playback_.update(snapshot,
                     tick,
                     playing,
                     playbackStartUs,
                     tickPeriodUsForTempo(config.tempo),
                     false);
    drainRealtimeMidiQueue_(core::time_compat::micros());
    OC_PERF_UNITS(perfTimer, pendingClockCount, playing ? 1U : 0U);
}

void SequencerInternalTimerLane::drainRealtimeMidiQueue_(uint32_t nowUs) {
    midi_queue_.drainDue(midi_, nowUs);
    midi_.serviceOutput(RealtimeMidiQueue::MAX_DRAIN_BUDGET_US);
}

}  // namespace core::sequencer

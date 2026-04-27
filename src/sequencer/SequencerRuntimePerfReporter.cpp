#include "sequencer/SequencerRuntimePerfReporter.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/realtime/InterruptGuard.hpp>

namespace core::sequencer {

void SequencerRuntimePerfReporter::record(uint32_t updateUs,
                                          uint32_t clockUs,
                                          uint32_t playbackUs,
                                          bool resyncRequested,
                                          uint32_t nowMs) {
    if (window_.startMs == 0) {
        window_.reset(nowMs);
    }

    window_.updateCount += 1;
    window_.totalUpdateUs += updateUs;
    window_.maxUpdateUs = std::max(window_.maxUpdateUs, updateUs);
    window_.totalClockUs += clockUs;
    window_.maxClockUs = std::max(window_.maxClockUs, clockUs);
    window_.totalPlaybackUs += playbackUs;
    window_.maxPlaybackUs = std::max(window_.maxPlaybackUs, playbackUs);
    if (resyncRequested) {
        window_.resyncCount += 1;
    }
}

void SequencerRuntimePerfReporter::flush(uint32_t nowMs,
                                         MidiClockSyncService& midiClockSync,
                                         RealtimeMidiQueue& midiQueue,
                                         SequencerInternalTimerLane& timerLane) {
#if !defined(PERF_LOG)
    resetInputs_(nowMs, midiClockSync, midiQueue, timerLane);
    return;
#endif

    if (window_.startMs == 0) {
        resetInputs_(nowMs, midiClockSync, midiQueue, timerLane);
        return;
    }

    if ((nowMs - window_.startMs) < 1000) {
        return;
    }

    const uint32_t avgUpdateUs =
        window_.updateCount > 0 ? (window_.totalUpdateUs / window_.updateCount) : 0;
    const uint32_t avgClockUs =
        window_.updateCount > 0 ? (window_.totalClockUs / window_.updateCount) : 0;
    const uint32_t avgPlaybackUs =
        window_.updateCount > 0 ? (window_.totalPlaybackUs / window_.updateCount) : 0;

    bool shouldLog = window_.maxUpdateUs >= 1000 || window_.maxClockUs >= 1000 ||
                     window_.maxPlaybackUs >= 1000 || window_.resyncCount > 0;
    const auto externalClockTelemetry = midiClockSync.takeExternalClockTelemetry();
    shouldLog = shouldLog || externalClockTelemetry.maxJitterUs >= 1000 ||
                externalClockTelemetry.maxHostGapMs >= 50;

#ifdef ARDUINO
    const auto timerProfiling = timerLane.takeProfiling(nowMs);

    const uint32_t avgTimerCallbackUs = timerProfiling.callbackCount > 0
                                            ? (timerProfiling.totalCallbackUs /
                                               timerProfiling.callbackCount)
                                            : 0;
    const uint32_t avgTimerPlaybackUs = timerProfiling.callbackCount > 0
                                            ? (timerProfiling.totalPlaybackUs /
                                               timerProfiling.callbackCount)
                                            : 0;
    const uint32_t avgTimerDrainUs = timerProfiling.callbackCount > 0
                                         ? (timerProfiling.totalBackendDrainUs /
                                            timerProfiling.callbackCount)
                                         : 0;

    shouldLog = shouldLog || timerProfiling.maxCallbackUs >= 1000 ||
                timerProfiling.maxPlaybackUs >= 1000 ||
                timerProfiling.maxBackendDrainUs >= 1000 ||
                timerProfiling.clockBurstClampCount > 0;
#else
    (void)timerLane;
#endif

    RealtimeMidiQueue::Counters queueCounters{};
#ifdef ARDUINO
    {
        oc::realtime::InterruptGuard lock;
        queueCounters = midiQueue.takeCounters();
    }
#else
    queueCounters = midiQueue.takeCounters();
#endif

    shouldLog = shouldLog || queueCounters.lateSent > 0 || queueCounters.dropped > 0 ||
                queueCounters.overflow > 0 || queueCounters.maxDrainUs >= 1000;

    if (shouldLog) {
        OC_LOG_INFO("[Perf][SequencerRuntime] updates={} avgUpdate={}us maxUpdate={}us avgClock={}us maxClock={}us avgPlayback={}us maxPlayback={}us resyncs={}",
                    window_.updateCount,
                    avgUpdateUs,
                    window_.maxUpdateUs,
                    avgClockUs,
                    window_.maxClockUs,
                    avgPlaybackUs,
                    window_.maxPlaybackUs,
                    window_.resyncCount);
#ifdef ARDUINO
        if (timerProfiling.callbackCount > 0) {
            OC_LOG_INFO("[Perf][SequencerTimer] callbacks={} avgCallback={}us maxCallback={}us avgPlayback={}us maxPlayback={}us avgDrain={}us maxDrain={}us maxClockBurst={} burstClamps={}",
                        timerProfiling.callbackCount,
                        avgTimerCallbackUs,
                        timerProfiling.maxCallbackUs,
                        avgTimerPlaybackUs,
                        timerProfiling.maxPlaybackUs,
                        avgTimerDrainUs,
                        timerProfiling.maxBackendDrainUs,
                        timerProfiling.maxClockBurst,
                        timerProfiling.clockBurstClampCount);
        }
#endif
        if (queueCounters.pushed > 0 || queueCounters.overflow > 0 ||
            queueCounters.dropped > 0 || queueCounters.cancelledNoteOns > 0) {
            OC_LOG_INFO("[Perf][RealtimeMidiQueue] pushed={} sent={} lateSent={} dropped={} cancelledNoteOns={} overflow={} highWater={} maxDrain={}us",
                        queueCounters.pushed,
                        queueCounters.sent,
                        queueCounters.lateSent,
                        queueCounters.dropped,
                        queueCounters.cancelledNoteOns,
                        queueCounters.overflow,
                        queueCounters.highWater,
                        queueCounters.maxDrainUs);
        }
        if (externalClockTelemetry.clockCount > 0) {
            OC_LOG_INFO("[Perf][ExternalClock] clocks={} maxInterval={}us maxHostGap={}ms maxJitter={}us",
                        externalClockTelemetry.clockCount,
                        externalClockTelemetry.maxIntervalUs,
                        externalClockTelemetry.maxHostGapMs,
                        externalClockTelemetry.maxJitterUs);
        }
    }

    window_.reset(nowMs);
}

void SequencerRuntimePerfReporter::logPlaybackSnapshot(
    const SequencerPlaybackService::ProfilingSnapshot& snapshot
) const {
#if defined(PERF_LOG)
    OC_LOG_INFO(
        "[Perf][SequencerPlayback] updates={} avgUpdate={}us maxUpdate={}us noteOns={} "
        "noteOffs={} panicOffs={} lateNotes={} queuedEvents={} "
        "tickJumpMax={} burstMax={}",
        snapshot.updateCount,
        snapshot.avgUpdateUs,
        snapshot.maxUpdateUs,
        snapshot.noteOnCount,
        snapshot.noteOffCount,
        snapshot.panicNoteOffCount,
        snapshot.lateNoteOnCount,
        snapshot.queuedEventCount,
        snapshot.maxTickJump,
        snapshot.maxNoteBurst
    );
#else
    (void)snapshot;
#endif
}

void SequencerRuntimePerfReporter::resetInputs_(uint32_t nowMs,
                                                MidiClockSyncService& midiClockSync,
                                                RealtimeMidiQueue& midiQueue,
                                                SequencerInternalTimerLane& timerLane) {
    window_.reset(nowMs);
    midiClockSync.takeExternalClockTelemetry();
#ifdef ARDUINO
    timerLane.resetProfiling(nowMs);
    {
        oc::realtime::InterruptGuard lock;
        midiQueue.takeCounters();
    }
#else
    (void)timerLane;
    midiQueue.takeCounters();
#endif
}

}  // namespace core::sequencer

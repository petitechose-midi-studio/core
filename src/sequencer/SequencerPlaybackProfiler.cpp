#include "sequencer/SequencerPlaybackProfiler.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>

namespace core::sequencer {

void SequencerPlaybackProfiler::Window::reset(uint32_t nowMs) {
    startMs = nowMs;
    updateCount = 0;
    totalUpdateUs = 0;
    maxUpdateUs = 0;
    noteOnCount = 0;
    noteOffCount = 0;
    panicNoteOffCount = 0;
    lateNoteOnCount = 0;
    maxTickJump = 0;
    maxNoteBurst = 0;
    queuedEventCount = 0;
}

void SequencerPlaybackProfiler::record(uint32_t tick,
                                       bool playing,
                                       uint32_t updateUs,
                                       const SequencerPlaybackActivitySnapshot& activity,
                                       uint32_t nowMs) {
    if (window_.startMs == 0) {
        window_.reset(nowMs);
    }

    window_.updateCount += 1;
    window_.totalUpdateUs += updateUs;
    window_.maxUpdateUs = std::max(window_.maxUpdateUs, updateUs);
    window_.noteOnCount += activity.noteOnCount;
    window_.noteOffCount += activity.noteOffCount;
    window_.panicNoteOffCount += activity.panicNoteOffCount;
    window_.maxNoteBurst = std::max(window_.maxNoteBurst, activity.noteOnCount);
    window_.queuedEventCount += activity.queuedEventCount;

    if (playing && lastTickValid_) {
        const uint32_t tickJump = (tick >= lastTick_) ? (tick - lastTick_) : 0;
        window_.maxTickJump = std::max(window_.maxTickJump, tickJump);
        if (tickJump > 1) {
            window_.lateNoteOnCount += activity.noteOnCount;
        }
    }

    lastTick_ = tick;
    lastTickValid_ = true;
}

bool SequencerPlaybackProfiler::takeSnapshot(uint32_t nowMs, Snapshot& snapshot) {
    if (window_.startMs == 0) {
        window_.reset(nowMs);
        return false;
    }

    if ((nowMs - window_.startMs) < 1000) {
        return false;
    }

    snapshot.updateCount = window_.updateCount;
    snapshot.avgUpdateUs =
        window_.updateCount > 0 ? (window_.totalUpdateUs / window_.updateCount) : 0;
    snapshot.maxUpdateUs = window_.maxUpdateUs;
    snapshot.noteOnCount = window_.noteOnCount;
    snapshot.noteOffCount = window_.noteOffCount;
    snapshot.panicNoteOffCount = window_.panicNoteOffCount;
    snapshot.lateNoteOnCount = window_.lateNoteOnCount;
    snapshot.queuedEventCount = window_.queuedEventCount;
    snapshot.maxTickJump = window_.maxTickJump;
    snapshot.maxNoteBurst = window_.maxNoteBurst;

    window_.reset(nowMs);

    return snapshot.noteOnCount > 0 || snapshot.maxUpdateUs >= 1000 || snapshot.maxTickJump > 1;
}

void SequencerPlaybackProfiler::maybeLog(uint32_t nowMs) {
#if !defined(PERF_LOG)
    window_.reset(nowMs);
    return;
#endif

    Snapshot snapshot{};
    if (!takeSnapshot(nowMs, snapshot)) {
        return;
    }

    OC_LOG_INFO("[Perf][SequencerPlayback] updates={} avgUpdate={}us maxUpdate={}us noteOns={} noteOffs={} panicOffs={} lateNotes={} queuedEvents={} tickJumpMax={} burstMax={}",
                snapshot.updateCount,
                snapshot.avgUpdateUs,
                snapshot.maxUpdateUs,
                snapshot.noteOnCount,
                snapshot.noteOffCount,
                snapshot.panicNoteOffCount,
                snapshot.lateNoteOnCount,
                snapshot.queuedEventCount,
                snapshot.maxTickJump,
                snapshot.maxNoteBurst);
}

}  // namespace core::sequencer

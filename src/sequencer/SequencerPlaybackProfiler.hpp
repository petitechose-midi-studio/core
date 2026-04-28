#pragma once

#include <cstdint>

namespace core::sequencer {

/**
 * Playback activity counters collected during a single playback update.
 *
 * This is input to profiling only. It should describe note activity already
 * queued by playback, not trigger new MIDI or state mutations.
 */
struct SequencerPlaybackActivitySnapshot {
    uint32_t noteOnCount = 0;
    uint32_t noteOffCount = 0;
    uint32_t panicNoteOffCount = 0;
    uint32_t queuedEventCount = 0;
};

/**
 * Windowed profiler for sequencer playback updates.
 *
 * The profiler tracks update cost, note activity, and tick jumps so the runtime
 * can log late/bursty playback behavior. It is diagnostic-only and must remain
 * outside the musical scheduling contract.
 */
class SequencerPlaybackProfiler {
public:
    struct Snapshot {
        uint32_t updateCount = 0;
        uint32_t avgUpdateUs = 0;
        uint32_t maxUpdateUs = 0;
        uint32_t noteOnCount = 0;
        uint32_t noteOffCount = 0;
        uint32_t panicNoteOffCount = 0;
        uint32_t lateNoteOnCount = 0;
        uint32_t queuedEventCount = 0;
        uint32_t maxTickJump = 0;
        uint32_t maxNoteBurst = 0;
    };

    void record(uint32_t tick,
                bool playing,
                uint32_t updateUs,
                const SequencerPlaybackActivitySnapshot& activity,
                uint32_t nowMs);
    bool takeSnapshot(uint32_t nowMs, Snapshot& snapshot);
    void maybeLog(uint32_t nowMs);

private:
    struct Window {
        uint32_t startMs = 0;
        uint32_t updateCount = 0;
        uint32_t totalUpdateUs = 0;
        uint32_t maxUpdateUs = 0;
        uint32_t noteOnCount = 0;
        uint32_t noteOffCount = 0;
        uint32_t panicNoteOffCount = 0;
        uint32_t lateNoteOnCount = 0;
        uint32_t maxTickJump = 0;
        uint32_t maxNoteBurst = 0;
        uint32_t queuedEventCount = 0;

        void reset(uint32_t nowMs);
    };

    Window window_{};
    uint32_t lastTick_ = 0;
    bool lastTickValid_ = false;
};

}  // namespace core::sequencer

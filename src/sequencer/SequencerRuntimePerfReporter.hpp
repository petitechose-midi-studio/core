#pragma once

#include <cstdint>

#include "sequencer/MidiClockSyncService.hpp"
#include "sequencer/RealtimeMidiQueue.hpp"
#include "sequencer/SequencerInternalTimerLane.hpp"
#include "sequencer/SequencerPlaybackService.hpp"

namespace core::sequencer {

class SequencerRuntimePerfReporter {
public:
    void record(uint32_t updateUs,
                uint32_t clockUs,
                uint32_t playbackUs,
                bool resyncRequested,
                uint32_t nowMs);
    void flush(uint32_t nowMs,
               MidiClockSyncService& midiClockSync,
               RealtimeMidiQueue& midiQueue,
               SequencerInternalTimerLane& timerLane);
    void logPlaybackSnapshot(const SequencerPlaybackService::ProfilingSnapshot& snapshot) const;

private:
    struct Window {
        uint32_t startMs = 0;
        uint32_t updateCount = 0;
        uint32_t totalUpdateUs = 0;
        uint32_t maxUpdateUs = 0;
        uint32_t totalClockUs = 0;
        uint32_t maxClockUs = 0;
        uint32_t totalPlaybackUs = 0;
        uint32_t maxPlaybackUs = 0;
        uint32_t resyncCount = 0;

        void reset(uint32_t nowMs) {
            startMs = nowMs;
            updateCount = 0;
            totalUpdateUs = 0;
            maxUpdateUs = 0;
            totalClockUs = 0;
            maxClockUs = 0;
            totalPlaybackUs = 0;
            maxPlaybackUs = 0;
            resyncCount = 0;
        }
    };

    void resetInputs_(uint32_t nowMs,
                      MidiClockSyncService& midiClockSync,
                      RealtimeMidiQueue& midiQueue,
                      SequencerInternalTimerLane& timerLane);

    Window window_{};
};

}  // namespace core::sequencer

#pragma once

#include <array>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IEventBus.hpp>

#include "sequencer/MidiClockSyncService.hpp"
#include "sequencer/SequencerPlaybackService.hpp"
#include "state/CoreState.hpp"

namespace core::sequencer {

class SequencerRuntimeService {
public:
    SequencerRuntimeService(core::state::CoreState& coreState,
                            oc::api::MidiAPI& midi,
                            oc::interface::IEventBus& eventBus);
    ~SequencerRuntimeService();

    SequencerRuntimeService(const SequencerRuntimeService&) = delete;
    SequencerRuntimeService& operator=(const SequencerRuntimeService&) = delete;
    SequencerRuntimeService(SequencerRuntimeService&&) = delete;
    SequencerRuntimeService& operator=(SequencerRuntimeService&&) = delete;

    void update();
    void stop();

private:
    struct ProfilingWindow {
        uint32_t window_start_ms = 0;
        uint32_t update_count = 0;
        uint32_t total_update_us = 0;
        uint32_t max_update_us = 0;
        uint32_t resync_count = 0;

        void resetWindow(uint32_t nowMs) {
            window_start_ms = nowMs;
            update_count = 0;
            total_update_us = 0;
            max_update_us = 0;
            resync_count = 0;
        }
    };

    void subscribeToMidiEvents_();
    void unsubscribeFromMidiEvents_();
    void recordProfilingWindow_(uint32_t updateUs, bool resyncRequested, uint32_t nowMs);
    void maybeLogProfilingWindow_(uint32_t nowMs);

    oc::interface::IEventBus& event_bus_;
    MidiClockSyncService midi_clock_sync_;
    SequencerPlaybackService sequencer_playback_;
    ProfilingWindow profiling_{};
    std::array<oc::interface::SubscriptionID, 4> midi_subscription_ids_{};
};

}  // namespace core::sequencer

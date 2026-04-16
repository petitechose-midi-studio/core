#pragma once

#include <array>

#ifdef ARDUINO
    #include <IntervalTimer.h>
#endif

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IEventBus.hpp>

#include "sequencer/MidiClockSyncService.hpp"
#include "sequencer/SequencerPlaybackService.hpp"
#include "sequencer/SequencerRuntimeStateSync.hpp"
#include "state/MidiSyncState.hpp"
#include "state/StatusBarState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::sequencer {

class SequencerRuntimeService {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& trackBank;
        core::state::StatusBarState& statusBar;
        core::state::MidiSyncState& midiSync;
    };

    SequencerRuntimeService(StateRefs state,
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
    MidiClockSyncRuntimeConfig captureClockSyncRuntimeConfig_() const;
    bool updateClockDomainOwnership_(const MidiClockSyncRuntimeConfig& config, uint32_t nowMs);
    uint8_t refreshTrackBankSnapshot_();
    void commitRuntimeSnapshot_(uint8_t snapshotIndex);
    void publishPlaybackUiFromTimerPath_(uint32_t nowMs);
#ifdef ARDUINO
    bool syncInternalTimer_(bool enable);
    void publishInternalTimerInputs_(const MidiClockSyncRuntimeConfig& config, uint8_t snapshotIndex);
    void onInternalTimer_();
#endif
    struct ProfilingWindow {
        uint32_t window_start_ms = 0;
        uint32_t update_count = 0;
        uint32_t total_update_us = 0;
        uint32_t max_update_us = 0;
        uint32_t total_clock_us = 0;
        uint32_t max_clock_us = 0;
        uint32_t total_playback_us = 0;
        uint32_t max_playback_us = 0;
        uint32_t resync_count = 0;

        void resetWindow(uint32_t nowMs) {
            window_start_ms = nowMs;
            update_count = 0;
            total_update_us = 0;
            max_update_us = 0;
            total_clock_us = 0;
            max_clock_us = 0;
            total_playback_us = 0;
            max_playback_us = 0;
            resync_count = 0;
        }
    };

    void subscribeToMidiEvents_();
    void unsubscribeFromMidiEvents_();
    void recordProfilingWindow_(uint32_t updateUs,
                                uint32_t clockUs,
                                uint32_t playbackUs,
                                bool resyncRequested,
                                uint32_t nowMs);
    void maybeLogProfilingWindow_(uint32_t nowMs);

    oc::interface::IEventBus& event_bus_;
    oc::api::MidiAPI& midi_;
    core::state::sequencer::SequencerState& sequencer_state_;
    core::state::sequencer::SequencerTrackBankState& track_bank_state_;
    core::state::StatusBarState& status_bar_state_;
    core::state::MidiSyncState& midi_sync_state_;
    MidiClockSyncService midi_clock_sync_;
    SequencerPlaybackService sequencer_playback_;
    std::array<core::state::sequencer::SequencerTrackBankSnapshot, 2> runtime_snapshots_{};
    std::array<SequencerRuntimeStateSignature, SequencerPlaybackService::TRACK_COUNT>
        runtime_track_bank_signatures_{};
    volatile uint8_t runtime_snapshot_index_ = 0;
#ifdef ARDUINO
    static constexpr uint32_t INTERNAL_TIMER_PERIOD_US = 1000;
    static constexpr uint32_t MAX_CLOCK_BURST_PER_UPDATE = 96;

    IntervalTimer internal_timer_{};
    InternalTransportClock internal_transport_clock_{};
    std::array<MidiClockSyncRuntimeConfig, 2> internal_timer_configs_{};
    bool internal_timer_running_ = false;
    bool internal_timer_playing_ = false;
    uint32_t internal_timer_last_tick_sent_ = 0;
#endif
    ProfilingWindow profiling_{};
    std::array<oc::interface::SubscriptionID, 4> midi_subscription_ids_{};
};

}  // namespace core::sequencer

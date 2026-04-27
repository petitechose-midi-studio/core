#pragma once

#include <array>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IEventBus.hpp>

#include "sequencer/MidiClockSyncService.hpp"
#include "sequencer/RealtimeMidiQueue.hpp"
#include "sequencer/SequencerInternalTimerLane.hpp"
#include "sequencer/SequencerPlaybackService.hpp"
#include "sequencer/SequencerRuntimePerfReporter.hpp"
#include "sequencer/SequencerRuntimeSnapshotBank.hpp"
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
    void publishPlaybackUiFromTimerPath_(uint32_t nowMs);
    void drainRealtimeMidiQueue_(uint32_t nowUs);

    void subscribeToMidiEvents_();
    void unsubscribeFromMidiEvents_();

    oc::interface::IEventBus& event_bus_;
    oc::api::MidiAPI& midi_;
    core::state::sequencer::SequencerState& sequencer_state_;
    core::state::StatusBarState& status_bar_state_;
    core::state::MidiSyncState& midi_sync_state_;
    MidiClockSyncService midi_clock_sync_;
    RealtimeMidiQueue midi_event_queue_{};
    SequencerRuntimeSnapshotBank snapshot_bank_;
    SequencerPlaybackService sequencer_playback_;
    SequencerInternalTimerLane internal_timer_lane_;
    SequencerRuntimePerfReporter perf_reporter_{};
    std::array<oc::interface::SubscriptionID, 4> midi_subscription_ids_{};
};

}  // namespace core::sequencer

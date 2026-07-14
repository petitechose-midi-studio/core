#pragma once

#include <array>
#include <memory>

#include <oc/api/MidiAPI.hpp>
#include <oc/interface/IEventBus.hpp>
#include <oc/state/Signal.hpp>

#include "sequencer/MidiClockSyncService.hpp"
#include "sequencer/SequencerRuntimeGraphBank.hpp"
#include "sequencer/SequencerRuntimeSnapshotBank.hpp"
#include "sequencer/SequencerRuntimeStateSync.hpp"
#include "state/MidiSyncState.hpp"
#include "state/StatusBarState.hpp"
#include "state/project/ProjectNavigationState.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerTrackActivationQueue.hpp"
#include "state/sequencer/SequencerTrackBankState.hpp"

namespace core::handler {
class MidiCcGlobalFrameCoordinator;
}

namespace core::sequencer {

class SequencerRealtimeLane;

/**
 * Standalone sequencer runtime orchestrator.
 *
 * Ownership is intentionally singular: `main.cpp` owns the live standalone
 * instance and ticks it from the app pre-context hook. `StandaloneContext`
 * assembles UI/features but must not become a second runtime execution path.
 *
 * The service receives only the state slices it mutates/publishes through
 * `StateRefs`; avoid widening this to `CoreState&` unless the runtime contract
 * itself changes.
 */
class SequencerRuntimeService {
public:
    struct StateRefs {
        core::state::sequencer::SequencerState& sequencer;
        core::state::sequencer::SequencerTrackBankState& trackBank;
        core::state::project::ProjectNavigationState& projectNavigation;
        core::state::StatusBarState& statusBar;
        core::state::MidiSyncState& midiSync;
        core::state::sequencer::SequencerTrackActivationQueue& trackActivations;
        core::handler::MidiCcGlobalFrameCoordinator** ccCoordinatorPublication = nullptr;
        const oc::state::Signal<uint32_t>* runtimeProjectRevision = nullptr;
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
    void stopPlayback_();
    void drainRealtimeMidiQueue_(uint32_t nowUs);
    void drainRealtimeMidiQueueFully_(uint32_t nowUs);
    void consumeProjectRuntimeReset_();

    void subscribeToMidiEvents_();
    void unsubscribeFromMidiEvents_();

    oc::interface::IEventBus& event_bus_;
    oc::api::MidiAPI& midi_;
    core::state::sequencer::SequencerState& sequencer_state_;
    core::state::sequencer::SequencerTrackBankState& track_bank_state_;
    core::state::StatusBarState& status_bar_state_;
    core::state::MidiSyncState& midi_sync_state_;
    core::state::sequencer::SequencerTrackActivationQueue& track_activations_;
    core::handler::MidiCcGlobalFrameCoordinator** cc_coordinator_publication_ = nullptr;
    const oc::state::Signal<uint32_t>* runtime_project_revision_ = nullptr;
    uint32_t consumed_runtime_project_revision_ = 0;
    MidiClockSyncService midi_clock_sync_;
    SequencerRuntimeGraphBank runtime_graph_bank_{};
    SequencerRuntimeSnapshotBank snapshot_bank_;
    // The service itself is PSRAM-backed because snapshots and graph ownership
    // are large. The timer lane, queue, and playback bookkeeping stay on the
    // internal heap through this indirection because they are touched at 1 kHz.
    std::unique_ptr<SequencerRealtimeLane> realtime_lane_;
    std::array<oc::interface::SubscriptionID, 4> midi_subscription_ids_{};
};

}  // namespace core::sequencer

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
    void subscribeToMidiEvents_();
    void unsubscribeFromMidiEvents_();

    oc::interface::IEventBus& event_bus_;
    MidiClockSyncService midi_clock_sync_;
    SequencerPlaybackService sequencer_playback_;
    std::array<oc::interface::SubscriptionID, 4> midi_subscription_ids_{};
};

}  // namespace core::sequencer

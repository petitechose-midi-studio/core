#include "SequencerRuntimeService.hpp"

#include <oc/core/event/Events.hpp>
#include <oc/time/Time.hpp>
#include <oc/type/Event.hpp>

namespace core::sequencer {

SequencerRuntimeService::SequencerRuntimeService(core::state::CoreState& coreState,
                                                 oc::api::MidiAPI& midi,
                                                 oc::interface::IEventBus& eventBus)
    : event_bus_(eventBus)
    , midi_clock_sync_(coreState.midiSync, coreState.statusBar, midi)
    , sequencer_playback_(coreState.sequencer, coreState.sequencerTracks, coreState.statusBar, midi) {
    subscribeToMidiEvents_();
}

SequencerRuntimeService::~SequencerRuntimeService() {
    stop();
    unsubscribeFromMidiEvents_();
}

void SequencerRuntimeService::update() {
    const uint32_t nowMs = oc::time::millis();
    midi_clock_sync_.update(nowMs);

    if (midi_clock_sync_.consumeResyncRequest()) {
        sequencer_playback_.stop();
    }

    sequencer_playback_.update(midi_clock_sync_.tick(), midi_clock_sync_.playing());
}

void SequencerRuntimeService::stop() {
    sequencer_playback_.stop();
}

void SequencerRuntimeService::subscribeToMidiEvents_() {
    using oc::core::event::MidiClockEvent;
    namespace MidiEvent = oc::core::event::MidiEvent;

    midi_subscription_ids_[0] = event_bus_.on(
        oc::type::EventCategory::MIDI,
        MidiEvent::CLOCK,
        [this](const oc::type::Event& event) {
            const auto& midiEvent = static_cast<const MidiClockEvent&>(event);
            midi_clock_sync_.onClock(midiEvent.timestampUs, oc::time::millis());
        }
    );

    midi_subscription_ids_[1] = event_bus_.on(
        oc::type::EventCategory::MIDI,
        MidiEvent::START,
        [this](const oc::type::Event&) {
            midi_clock_sync_.onStart();
        }
    );

    midi_subscription_ids_[2] = event_bus_.on(
        oc::type::EventCategory::MIDI,
        MidiEvent::CONTINUE,
        [this](const oc::type::Event&) {
            midi_clock_sync_.onContinue();
        }
    );

    midi_subscription_ids_[3] = event_bus_.on(
        oc::type::EventCategory::MIDI,
        MidiEvent::STOP,
        [this](const oc::type::Event&) {
            midi_clock_sync_.onStop();
        }
    );
}

void SequencerRuntimeService::unsubscribeFromMidiEvents_() {
    for (auto& id : midi_subscription_ids_) {
        if (id != 0) {
            event_bus_.off(id);
            id = 0;
        }
    }
}

}  // namespace core::sequencer

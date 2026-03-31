#include "SequencerRuntimeService.hpp"

#include <algorithm>

#include <oc/core/event/Events.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>
#include <oc/type/Event.hpp>

#include "config/TimeCompat.hpp"

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
    const uint32_t startUs = core::time_compat::micros();
    const uint32_t nowMs = oc::time::millis();
    midi_clock_sync_.update(nowMs);

    const bool resyncRequested = midi_clock_sync_.consumeResyncRequest();
    if (resyncRequested) {
        sequencer_playback_.stop();
    }

    sequencer_playback_.update(midi_clock_sync_.tick(), midi_clock_sync_.playing(), nowMs);
    recordProfilingWindow_(core::time_compat::micros() - startUs, resyncRequested, nowMs);
    maybeLogProfilingWindow_(nowMs);
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

void SequencerRuntimeService::recordProfilingWindow_(uint32_t updateUs,
                                                     bool resyncRequested,
                                                     uint32_t nowMs) {
    if (profiling_.window_start_ms == 0) {
        profiling_.resetWindow(nowMs);
    }

    profiling_.update_count += 1;
    profiling_.total_update_us += updateUs;
    profiling_.max_update_us = std::max(profiling_.max_update_us, updateUs);
    if (resyncRequested) {
        profiling_.resync_count += 1;
    }
}

void SequencerRuntimeService::maybeLogProfilingWindow_(uint32_t nowMs) {
    if (profiling_.window_start_ms == 0) {
        profiling_.resetWindow(nowMs);
        return;
    }

    if ((nowMs - profiling_.window_start_ms) < 1000) {
        return;
    }

    const uint32_t avgUpdateUs =
        profiling_.update_count > 0 ? (profiling_.total_update_us / profiling_.update_count) : 0;

    if (profiling_.max_update_us >= 1000 || profiling_.resync_count > 0) {
        OC_LOG_INFO("[Perf][SequencerRuntime] updates={} avgUpdate={}us maxUpdate={}us resyncs={}",
                    profiling_.update_count,
                    avgUpdateUs,
                    profiling_.max_update_us,
                    profiling_.resync_count);
    }

    profiling_.resetWindow(nowMs);
}

}  // namespace core::sequencer

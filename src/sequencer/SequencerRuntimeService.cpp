#include "SequencerRuntimeService.hpp"

#include <oc/core/event/Events.hpp>
#include <oc/log/Log.hpp>
#include <oc/realtime/InterruptGuard.hpp>
#include <oc/time/Time.hpp>
#include <oc/type/Event.hpp>

#include "config/TimeCompat.hpp"
#include "sequencer/SequencerTiming.hpp"

namespace core::sequencer {

namespace {

FLASHMEM void applyMidiClockSyncUiProjection(
    core::state::StatusBarState& statusBar,
    core::state::MidiSyncState& midiSync,
    const MidiClockSyncService::UiProjectionSnapshot& projection,
    uint32_t nowMs
) {
    if ((projection.dirtyMask & MidiClockSyncService::UiProjectionSnapshot::PLAYING) != 0) {
        statusBar.playing.set(projection.playing);
    }
    if ((projection.dirtyMask & MidiClockSyncService::UiProjectionSnapshot::TEMPO_DISPLAY) != 0) {
        statusBar.tempoDisplay.set(projection.tempoDisplay);
    }
    if ((projection.dirtyMask & MidiClockSyncService::UiProjectionSnapshot::SYNC_EXTERNAL_SOURCE) != 0) {
        statusBar.syncExternalSource.set(projection.syncExternalSource);
    }
    if ((projection.dirtyMask & MidiClockSyncService::UiProjectionSnapshot::TEMPO_LOCKED) != 0) {
        statusBar.tempoLocked.set(projection.tempoLocked);
    }
    if ((projection.dirtyMask & MidiClockSyncService::UiProjectionSnapshot::TRANSPORT_LOCKED) != 0) {
        statusBar.transportLocked.set(projection.transportLocked);
    }
    if ((projection.dirtyMask & MidiClockSyncService::UiProjectionSnapshot::ACTIVE_SOURCE) != 0) {
        midiSync.activeSource.set(projection.activeSource);
    }
    if ((projection.dirtyMask & MidiClockSyncService::UiProjectionSnapshot::EXTERNAL_CLOCK_PRESENT) != 0) {
        midiSync.externalClockPresent.set(projection.externalClockPresent);
    }
    if (projection.syncInputPulse) {
        statusBar.pulseSyncInput(nowMs);
    }
}

}  // namespace

FLASHMEM SequencerRuntimeService::SequencerRuntimeService(StateRefs state,
                                                          oc::api::MidiAPI& midi,
                                                          oc::interface::IEventBus& eventBus)
    : event_bus_(eventBus)
    , midi_(midi)
    , sequencer_state_(state.sequencer)
    , project_navigation_state_(state.projectNavigation)
    , status_bar_state_(state.statusBar)
    , midi_sync_state_(state.midiSync)
    , midi_clock_sync_(midi)
    , snapshot_bank_(state.sequencer, state.trackBank, state.projectNavigation)
    , sequencer_playback_(state.sequencer, state.trackBank, state.statusBar, midi_event_queue_)
    , internal_timer_lane_(midi, midi_event_queue_, snapshot_bank_, sequencer_playback_) {
    const uint8_t initialSnapshotIndex = snapshot_bank_.refresh();
    snapshot_bank_.commit(initialSnapshotIndex);
    subscribeToMidiEvents_();
}

FLASHMEM SequencerRuntimeService::~SequencerRuntimeService() {
    stop();
    unsubscribeFromMidiEvents_();
}

void SequencerRuntimeService::update() {
    const uint32_t startUs = core::time_compat::micros();
    const uint32_t nowUs = startUs;
    const uint32_t nowMs = oc::time::millis();
    const auto clockConfig = captureClockSyncRuntimeConfig_();
    const uint32_t tickPeriodUs = tickPeriodUsForTempo(clockConfig.tempo);
    const uint8_t snapshotIndex = snapshot_bank_.refresh();
    const auto& runtimeSnapshot = snapshot_bank_.snapshot(snapshotIndex);

    const uint32_t clockStartUs = core::time_compat::micros();
    const bool timerOwnsTransport = updateClockDomainOwnership_(clockConfig, nowMs);
    const uint32_t clockUs = core::time_compat::micros() - clockStartUs;

    const bool resyncRequested = midi_clock_sync_.consumeResyncRequest();

    uint32_t playbackUs = 0;
    bool usingInternalTimerPath = false;

#ifdef ARDUINO
    if (timerOwnsTransport) {
        internal_timer_lane_.publishRealtimeInputs(clockConfig, snapshotIndex);

        if (resyncRequested) {
            internal_timer_lane_.stop();
            sequencer_playback_.stop();
        }

        usingInternalTimerPath = internal_timer_lane_.start();
        if (!usingInternalTimerPath) {
            OC_LOG_WARN("{}", "[SequencerRuntime] failed to start internal playback timer");
            midi_clock_sync_.update(clockConfig, nowMs, true);
        }
    } else {
        internal_timer_lane_.stop();
    }
#endif

    if (usingInternalTimerPath) {
        const uint32_t playbackStartUs = core::time_compat::micros();
        publishPlaybackUiFromTimerPath_(nowMs);
        playbackUs = core::time_compat::micros() - playbackStartUs;
    } else {
        snapshot_bank_.commit(snapshotIndex);
        if (resyncRequested) {
            sequencer_playback_.stop();
        }

        const uint32_t playbackStartUs = core::time_compat::micros();
        sequencer_playback_.update(runtimeSnapshot,
                                   midi_clock_sync_.tick(),
                                   midi_clock_sync_.playing(),
                                   nowMs,
                                   nowUs,
                                   tickPeriodUs);
        playbackUs = core::time_compat::micros() - playbackStartUs;
        drainRealtimeMidiQueue_(core::time_compat::micros());
        sequencer_playback_.publishUiState(nowMs);
    }

    applyMidiClockSyncUiProjection(
        status_bar_state_,
        midi_sync_state_,
        midi_clock_sync_.takeUiProjectionSnapshot(),
        nowMs
    );
    const uint32_t updateUs = core::time_compat::micros() - startUs;

    perf_reporter_.record(updateUs, clockUs, playbackUs, resyncRequested, nowMs);
    perf_reporter_.flush(nowMs, midi_clock_sync_, midi_event_queue_, internal_timer_lane_);
}

FLASHMEM void SequencerRuntimeService::stop() {
#ifdef ARDUINO
    internal_timer_lane_.stop();
#endif
    sequencer_playback_.stop();
    drainRealtimeMidiQueue_(core::time_compat::micros());
}

MidiClockSyncRuntimeConfig SequencerRuntimeService::captureClockSyncRuntimeConfig_() const {
    return {
        .mode = midi_sync_state_.mode.get(),
        .followTransport = midi_sync_state_.followTransport.get(),
        .autoFallbackMs = midi_sync_state_.autoFallbackMs.get(),
        .autoLockClockCount = midi_sync_state_.autoLockClockCount.get(),
        .tempo = status_bar_state_.tempo.get(),
        .playing = status_bar_state_.playing.get(),
    };
}

bool SequencerRuntimeService::updateClockDomainOwnership_(
    const MidiClockSyncRuntimeConfig& config,
    uint32_t nowMs
) {
#ifdef ARDUINO
    const bool predictedTimerOwnsTransport =
        config.mode != core::state::MidiSyncMode::SLAVE && !midi_clock_sync_.usingExternalSource();

    midi_clock_sync_.update(config, nowMs, !predictedTimerOwnsTransport);

    const bool actualTimerOwnsTransport =
        config.mode != core::state::MidiSyncMode::SLAVE && !midi_clock_sync_.usingExternalSource();

    if (actualTimerOwnsTransport != predictedTimerOwnsTransport) {
        midi_clock_sync_.update(config, nowMs, !actualTimerOwnsTransport);
    }

    return actualTimerOwnsTransport;
#else
    midi_clock_sync_.update(config, nowMs);
    return false;
#endif
}

void SequencerRuntimeService::publishPlaybackUiFromTimerPath_(uint32_t nowMs) {
#ifdef ARDUINO
    SequencerPlaybackService::UiProjectionSnapshot uiProjection;
    SequencerRuntimeTelemetrySnapshot runtimeTelemetry;
    SequencerPlaybackService::ProfilingSnapshot profilingSnapshot{};
    bool shouldLogPlayback = false;

    // Pull the timer-lane projection under lock, then publish it outside the ISR.
    {
        oc::realtime::InterruptGuard lock;
        uiProjection = sequencer_playback_.takeUiProjectionSnapshot();
        runtimeTelemetry = sequencer_playback_.copyActiveRuntimeTelemetry();
        shouldLogPlayback = sequencer_playback_.takeProfilingSnapshot(nowMs, profilingSnapshot);
    }

    publishRuntimeTelemetry(sequencer_state_, runtimeTelemetry);
    sequencer_playback_.publishUiProjection(uiProjection, nowMs);

    if (shouldLogPlayback) {
        perf_reporter_.logPlaybackSnapshot(profilingSnapshot);
    }
#else
    sequencer_playback_.publishUiState(nowMs);
#endif
}

void SequencerRuntimeService::drainRealtimeMidiQueue_(uint32_t nowUs) {
    midi_event_queue_.drainDue(midi_, nowUs);
    midi_.serviceOutput(RealtimeMidiQueue::MAX_DRAIN_BUDGET_US);
}

FLASHMEM void SequencerRuntimeService::subscribeToMidiEvents_() {
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

FLASHMEM void SequencerRuntimeService::unsubscribeFromMidiEvents_() {
    for (auto& id : midi_subscription_ids_) {
        if (id != 0) {
            event_bus_.off(id);
            id = 0;
        }
    }
}

}  // namespace core::sequencer

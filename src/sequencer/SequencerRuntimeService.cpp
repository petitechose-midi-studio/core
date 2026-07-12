#include "SequencerRuntimeService.hpp"

#include <new>

#include <oc/core/event/Events.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/log/Log.hpp>
#include <oc/realtime/InterruptGuard.hpp>
#include <oc/time/Time.hpp>
#include <oc/type/Event.hpp>

#include "config/TimeCompat.hpp"
#include "sequencer/RealtimeMidiQueue.hpp"
#include "sequencer/SequencerInternalTimerLane.hpp"
#include "sequencer/SequencerPlaybackService.hpp"
#include "sequencer/SequencerTiming.hpp"

namespace core::sequencer {

class SequencerRealtimeLane {
public:
    SequencerRealtimeLane(core::state::sequencer::SequencerState& sequencer,
                          core::state::StatusBarState& statusBar,
                          oc::api::MidiAPI& midi,
                          SequencerRuntimeSnapshotBank& snapshotBank,
                          const SequencerRuntimeGraphBank& graphBank)
        : playback(sequencer, statusBar, midiQueue, graphBank)
        , timer(midi, midiQueue, snapshotBank, playback) {}

    RealtimeMidiQueue midiQueue{};
    SequencerPlaybackService playback;
    SequencerInternalTimerLane timer;
};

namespace {

[[noreturn]] FLASHMEM void failSequencerRuntimeAllocation() {
    OC_LOG_ERROR("{}", "[SequencerRuntime] RAM2 allocation failed");
    while (true) {}
}

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
    , track_bank_state_(state.trackBank)
    , status_bar_state_(state.statusBar)
    , midi_sync_state_(state.midiSync)
    , midi_clock_sync_(midi)
    , snapshot_bank_(state.sequencer, state.trackBank, state.projectNavigation)
    , realtime_lane_(new (std::nothrow) SequencerRealtimeLane(
          state.sequencer,
          state.statusBar,
          midi,
          snapshot_bank_,
          runtime_graph_bank_
      )) {
    if (!realtime_lane_) failSequencerRuntimeAllocation();
    // Initial flat state remains useful even if PSRAM is unavailable: root
    // steps can still play and the graph bank will retry on later updates.
    (void)runtime_graph_bank_.prepare(sequencer_state_, track_bank_state_);
    const uint8_t initialSnapshotIndex = snapshot_bank_.refresh();
    runtime_graph_bank_.publishPrepared([this, initialSnapshotIndex]() {
        snapshot_bank_.commit(initialSnapshotIndex);
    });
    subscribeToMidiEvents_();
}

FLASHMEM SequencerRuntimeService::~SequencerRuntimeService() {
    stop();
    unsubscribeFromMidiEvents_();
}

void SequencerRuntimeService::update() {
    OC_PERF_SCOPE(perfRuntime, "sequencer.runtime");
    const uint32_t nowUs = core::time_compat::micros();
    const uint32_t nowMs = oc::time::millis();
    const auto clockConfig = captureClockSyncRuntimeConfig_();
    const uint32_t tickPeriodUs = tickPeriodUsForTempo(clockConfig.tempo);
    const bool graphGenerationReady =
        runtime_graph_bank_.prepare(sequencer_state_, track_bank_state_);
    // Keep graph and flat data on the same published generation. On a rare
    // PSRAM allocation failure, retain the previous pair and retry next loop.
    const uint8_t snapshotIndex = graphGenerationReady
        ? snapshot_bank_.refresh()
        : snapshot_bank_.activeIndex();
    const auto& runtimeSnapshot = snapshot_bank_.snapshot(snapshotIndex);

    bool timerOwnsTransport = false;
    {
        OC_PERF_SCOPE(perfClock, "sequencer.clock-domain");
        timerOwnsTransport = updateClockDomainOwnership_(clockConfig, nowMs);
    }

    const bool resyncRequested = midi_clock_sync_.consumeResyncRequest();

    bool usingInternalTimerPath = false;

#ifdef ARDUINO
    if (timerOwnsTransport) {
        runtime_graph_bank_.publishPrepared([this, &clockConfig, snapshotIndex]() {
            realtime_lane_->timer.publishRealtimeInputs(clockConfig, snapshotIndex);
        });

        if (resyncRequested) {
            realtime_lane_->timer.stop();
            stopPlayback_();
        }

        usingInternalTimerPath = realtime_lane_->timer.start();
        if (!usingInternalTimerPath) {
            OC_LOG_WARN("{}", "[SequencerRuntime] failed to start internal playback timer");
            midi_clock_sync_.update(clockConfig, nowMs, true);
        }
    } else {
        realtime_lane_->timer.stop();
        runtime_graph_bank_.publishPrepared([this, snapshotIndex]() {
            snapshot_bank_.commit(snapshotIndex);
        });
    }
#endif

#ifndef ARDUINO
    runtime_graph_bank_.publishPrepared([this, snapshotIndex]() {
        snapshot_bank_.commit(snapshotIndex);
    });
#endif

    if (usingInternalTimerPath) {
        OC_PERF_SCOPE(perfTimerUi, "sequencer.timer-ui-projection");
        publishPlaybackUiFromTimerPath_(nowMs);
    } else {
        if (resyncRequested) {
            stopPlayback_();
        }

        realtime_lane_->playback.update(runtimeSnapshot,
                                        midi_clock_sync_.tick(),
                                        midi_clock_sync_.playing(),
                                        nowUs,
                                        tickPeriodUs);
        drainRealtimeMidiQueue_(core::time_compat::micros());
        realtime_lane_->playback.publishUiState(nowMs);
    }

    applyMidiClockSyncUiProjection(
        status_bar_state_,
        midi_sync_state_,
        midi_clock_sync_.takeUiProjectionSnapshot(),
        nowMs
    );
}

FLASHMEM void SequencerRuntimeService::stop() {
#ifdef ARDUINO
    realtime_lane_->timer.stop();
#endif
    stopPlayback_();
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

    // Pull the timer-lane projection under lock, then publish it outside the ISR.
    {
        oc::realtime::InterruptGuard lock;
        uiProjection = realtime_lane_->playback.takeUiProjectionSnapshot();
        runtimeTelemetry = realtime_lane_->playback.copyActiveRuntimeTelemetry();
    }

    publishRuntimeTelemetry(sequencer_state_, runtimeTelemetry);
    realtime_lane_->playback.publishUiProjection(uiProjection, nowMs);
#else
    realtime_lane_->playback.publishUiState(nowMs);
#endif
}

void SequencerRuntimeService::drainRealtimeMidiQueue_(uint32_t nowUs) {
    realtime_lane_->midiQueue.drainDue(midi_, nowUs);
    midi_.serviceOutput(RealtimeMidiQueue::MAX_DRAIN_BUDGET_US);
}

void SequencerRuntimeService::drainRealtimeMidiQueueFully_(uint32_t nowUs) {
    realtime_lane_->midiQueue.drainDue(midi_, nowUs, UINT32_MAX);
    midi_.serviceOutput(UINT32_MAX);
}

void SequencerRuntimeService::stopPlayback_() {
    // Future events no longer belong to the stopped generation. Drop them,
    // then drain each track's immediate panic note-offs before the next track
    // can fill the bounded realtime queue.
    realtime_lane_->midiQueue.clear();
    midi_.serviceOutput(UINT32_MAX);
    for (uint8_t track = 0; track < SequencerPlaybackService::TRACK_COUNT; ++track) {
        realtime_lane_->playback.stopTrack(track);
        drainRealtimeMidiQueueFully_(core::time_compat::micros());
    }
    realtime_lane_->playback.completeStop();
    midi_.allNotesOff();
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

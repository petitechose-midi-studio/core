#include "SequencerRuntimeService.hpp"

#include <algorithm>

#include <oc/core/event/Events.hpp>
#include <oc/log/Log.hpp>
#include <oc/time/Time.hpp>
#include <oc/type/Event.hpp>

#include "config/TimeCompat.hpp"
#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::sequencer {

namespace {

#ifdef ARDUINO

inline uint32_t readPrimask() {
    uint32_t primask = 0;
    asm volatile("MRS %0, primask" : "=r"(primask));
    return primask;
}

class InterruptLock {
public:
    InterruptLock()
        : primask_(readPrimask()) {
        __disable_irq();
    }

    ~InterruptLock() {
        asm volatile("MSR primask, %0" : : "r"(primask_) : "memory");
    }

    InterruptLock(const InterruptLock&) = delete;
    InterruptLock& operator=(const InterruptLock&) = delete;

private:
    uint32_t primask_ = 0;
};

#endif

FLASHMEM void logPlaybackProfilingSnapshot(
    const SequencerPlaybackService::ProfilingSnapshot& snapshot
) {
#if defined(PERF_LOG)
    OC_LOG_INFO(
        "[Perf][SequencerPlayback] updates={} avgUpdate={}us maxUpdate={}us noteOns={} "
        "noteOffs={} panicOffs={} lateNotes={} midiSendAvg={}us midiSendMax={}us "
        "tickJumpMax={} burstMax={}",
        snapshot.updateCount,
        snapshot.avgUpdateUs,
        snapshot.maxUpdateUs,
        snapshot.noteOnCount,
        snapshot.noteOffCount,
        snapshot.panicNoteOffCount,
        snapshot.lateNoteOnCount,
        snapshot.avgMidiSendUs,
        snapshot.maxMidiSendUs,
        snapshot.maxTickJump,
        snapshot.maxNoteBurst
    );
#else
    (void)snapshot;
#endif
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
    , sequencer_playback_(state.sequencer, state.trackBank, state.statusBar, midi) {
    const uint8_t initialSnapshotIndex = refreshTrackBankSnapshot_();
    commitRuntimeSnapshot_(initialSnapshotIndex);
#ifdef ARDUINO
    const auto clockConfig = captureClockSyncRuntimeConfig_();
    internal_timer_configs_[0] = clockConfig;
    internal_timer_configs_[1] = clockConfig;
#endif
    subscribeToMidiEvents_();
}

FLASHMEM SequencerRuntimeService::~SequencerRuntimeService() {
    stop();
    unsubscribeFromMidiEvents_();
}

void SequencerRuntimeService::update() {
    const uint32_t startUs = core::time_compat::micros();
    const uint32_t nowMs = oc::time::millis();
    const auto clockConfig = captureClockSyncRuntimeConfig_();
    const uint8_t snapshotIndex = refreshTrackBankSnapshot_();
    const auto& runtimeSnapshot = runtime_snapshots_[snapshotIndex];

    const uint32_t clockStartUs = core::time_compat::micros();
    const bool timerOwnsTransport = updateClockDomainOwnership_(clockConfig, nowMs);
    const uint32_t clockUs = core::time_compat::micros() - clockStartUs;

    const bool resyncRequested = midi_clock_sync_.consumeResyncRequest();

    uint32_t playbackUs = 0;
    bool usingInternalTimerPath = false;

#ifdef ARDUINO
    if (timerOwnsTransport) {
        publishInternalTimerInputs_(clockConfig, snapshotIndex);

        if (resyncRequested) {
            syncInternalTimer_(false);
            sequencer_playback_.stop();
        }

        usingInternalTimerPath = syncInternalTimer_(true);
        if (!usingInternalTimerPath) {
            midi_clock_sync_.update(clockConfig, nowMs, true);
        }
    } else {
        syncInternalTimer_(false);
    }
#endif

    if (usingInternalTimerPath) {
        const uint32_t playbackStartUs = core::time_compat::micros();
        // The timer callback already advanced transport/playback. This lane only
        // snapshots timer-produced telemetry and applies UI-facing projections.
        publishPlaybackUiFromTimerPath_(nowMs);
        playbackUs = core::time_compat::micros() - playbackStartUs;
    } else {
        commitRuntimeSnapshot_(snapshotIndex);
        if (resyncRequested) {
            sequencer_playback_.stop();
        }

        const uint32_t playbackStartUs = core::time_compat::micros();
        sequencer_playback_.update(runtimeSnapshot,
                                   midi_clock_sync_.tick(),
                                   midi_clock_sync_.playing(),
                                   nowMs);
        playbackUs = core::time_compat::micros() - playbackStartUs;
        sequencer_playback_.publishUiState(nowMs);
    }

    applyMidiClockSyncUiProjection(
        status_bar_state_,
        midi_sync_state_,
        midi_clock_sync_.takeUiProjectionSnapshot(),
        nowMs
    );
    const uint32_t updateUs = core::time_compat::micros() - startUs;

    recordProfilingWindow_(updateUs, clockUs, playbackUs, resyncRequested, nowMs);
    maybeLogProfilingWindow_(nowMs);
}

FLASHMEM void SequencerRuntimeService::stop() {
#ifdef ARDUINO
    syncInternalTimer_(false);
#endif
    sequencer_playback_.stop();
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

uint8_t SequencerRuntimeService::refreshTrackBankSnapshot_() {
    const uint8_t currentIndex = runtime_snapshot_index_;
    const uint8_t writeIndex = static_cast<uint8_t>(currentIndex ^ 0x1U);
    auto& runtimeSnapshot = runtime_snapshots_[writeIndex];
    runtimeSnapshot = runtime_snapshots_[currentIndex];

    const uint8_t activeTrack =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            track_bank_state_.activeTrackIndex()
        );

    runtimeSnapshot.activeTrack = activeTrack;
    runtimeSnapshot.enabledMask = track_bank_state_.currentEnabledMask();

    for (uint8_t i = 0; i < runtimeSnapshot.tracks.size(); ++i) {
        const auto& source = (i == activeTrack) ? sequencer_state_ : track_bank_state_.track(i);
        const auto signature = captureRuntimeStateSignature(source);
        if (runtime_track_bank_signatures_[i].matches(signature)) {
            continue;
        }

        core::state::sequencer::captureSnapshot(source, runtimeSnapshot.tracks[i]);
        runtime_track_bank_signatures_[i] = signature;
    }

    return writeIndex;
}

void SequencerRuntimeService::commitRuntimeSnapshot_(uint8_t snapshotIndex) {
#ifdef ARDUINO
    InterruptLock lock;
    runtime_snapshot_index_ = snapshotIndex;
#else
    runtime_snapshot_index_ = snapshotIndex;
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
        InterruptLock lock;
        uiProjection = sequencer_playback_.takeUiProjectionSnapshot();
        runtimeTelemetry = sequencer_playback_.copyActiveRuntimeTelemetry();
        shouldLogPlayback = sequencer_playback_.takeProfilingSnapshot(nowMs, profilingSnapshot);
    }

    publishRuntimeTelemetry(sequencer_state_, runtimeTelemetry);
    sequencer_playback_.publishUiProjection(uiProjection, nowMs);

    if (shouldLogPlayback) {
        logPlaybackProfilingSnapshot(profilingSnapshot);
    }
#else
    sequencer_playback_.publishUiState(nowMs);
#endif
}

#ifdef ARDUINO

FLASHMEM bool SequencerRuntimeService::syncInternalTimer_(bool enable) {
    if (!enable) {
        if (internal_timer_running_) {
            internal_timer_.end();
            internal_timer_running_ = false;
        }

        internal_transport_clock_.reset();
        internal_timer_playing_ = false;
        internal_timer_last_tick_sent_ = 0;
        return true;
    }

    if (internal_timer_running_) {
        return true;
    }

    internal_transport_clock_.reset();
    internal_timer_playing_ = false;
    internal_timer_last_tick_sent_ = 0;
    internal_timer_.priority(128);
    internal_timer_running_ =
        internal_timer_.begin([this]() { onInternalTimer_(); }, INTERNAL_TIMER_PERIOD_US);

    if (!internal_timer_running_) {
        OC_LOG_WARN("{}", "[SequencerRuntime] failed to start internal playback timer");
        return false;
    }

    return true;
}

void SequencerRuntimeService::publishInternalTimerInputs_(const MidiClockSyncRuntimeConfig& config,
                                                          uint8_t snapshotIndex) {
    internal_timer_configs_[snapshotIndex] = config;
    commitRuntimeSnapshot_(snapshotIndex);
}

void SequencerRuntimeService::onInternalTimer_() {
    // Internal master timer lane: keep this limited to transport advancement,
    // playback scheduling, and backend output drain. Do not add UI/state writes here.
    const uint8_t inputIndex = runtime_snapshot_index_;
    const auto& snapshot = runtime_snapshots_[inputIndex];
    const auto config = internal_timer_configs_[inputIndex];

    internal_transport_clock_.setBpm(config.tempo);
    internal_transport_clock_.setPlaying(config.playing);

    const bool playing = internal_transport_clock_.isPlaying();
    const uint32_t tick = internal_transport_clock_.tick();

    if (playing && !internal_timer_playing_) {
        midi_.sendStart();
    } else if (!playing && internal_timer_playing_) {
        midi_.sendStop();
    }

    internal_timer_playing_ = playing;

    if (!playing) {
        internal_timer_last_tick_sent_ = tick;
    } else {
        if (tick < internal_timer_last_tick_sent_) {
            internal_timer_last_tick_sent_ = tick;
        }

        uint32_t pendingClockCount = tick - internal_timer_last_tick_sent_;
        if (pendingClockCount > MAX_CLOCK_BURST_PER_UPDATE) {
            pendingClockCount = MAX_CLOCK_BURST_PER_UPDATE;
        }

        for (uint32_t i = 0; i < pendingClockCount; ++i) {
            midi_.sendClock();
            internal_timer_last_tick_sent_ += 1U;
        }
    }

    sequencer_playback_.update(snapshot,
                               tick,
                               playing,
                               core::time_compat::millis(),
                               false,
                               false);
    midi_.serviceOutput();
}

#endif

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

void SequencerRuntimeService::recordProfilingWindow_(uint32_t updateUs,
                                                     uint32_t clockUs,
                                                     uint32_t playbackUs,
                                                     bool resyncRequested,
                                                     uint32_t nowMs) {
    if (profiling_.window_start_ms == 0) {
        profiling_.resetWindow(nowMs);
    }

    profiling_.update_count += 1;
    profiling_.total_update_us += updateUs;
    profiling_.max_update_us = std::max(profiling_.max_update_us, updateUs);
    profiling_.total_clock_us += clockUs;
    profiling_.max_clock_us = std::max(profiling_.max_clock_us, clockUs);
    profiling_.total_playback_us += playbackUs;
    profiling_.max_playback_us = std::max(profiling_.max_playback_us, playbackUs);
    if (resyncRequested) {
        profiling_.resync_count += 1;
    }
}

FLASHMEM void SequencerRuntimeService::maybeLogProfilingWindow_(uint32_t nowMs) {
#if !defined(PERF_LOG)
    profiling_.resetWindow(nowMs);
    return;
#endif

    if (profiling_.window_start_ms == 0) {
        profiling_.resetWindow(nowMs);
        return;
    }

    if ((nowMs - profiling_.window_start_ms) < 1000) {
        return;
    }

    const uint32_t avgUpdateUs =
        profiling_.update_count > 0 ? (profiling_.total_update_us / profiling_.update_count) : 0;
    const uint32_t avgClockUs =
        profiling_.update_count > 0 ? (profiling_.total_clock_us / profiling_.update_count) : 0;
    const uint32_t avgPlaybackUs =
        profiling_.update_count > 0 ? (profiling_.total_playback_us / profiling_.update_count) : 0;

    if (profiling_.max_update_us >= 1000 || profiling_.max_clock_us >= 1000 ||
        profiling_.max_playback_us >= 1000 || profiling_.resync_count > 0) {
        OC_LOG_INFO("[Perf][SequencerRuntime] updates={} avgUpdate={}us maxUpdate={}us avgClock={}us maxClock={}us avgPlayback={}us maxPlayback={}us resyncs={}",
                    profiling_.update_count,
                    avgUpdateUs,
                    profiling_.max_update_us,
                    avgClockUs,
                    profiling_.max_clock_us,
                    avgPlaybackUs,
                    profiling_.max_playback_us,
                    profiling_.resync_count);
    }

    profiling_.resetWindow(nowMs);
}

}  // namespace core::sequencer

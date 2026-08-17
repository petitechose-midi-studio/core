#include "SequencerRuntimeService.hpp"

#include <new>

#include <oc/core/event/Events.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/log/Log.hpp>
#include <oc/realtime/InterruptGuard.hpp>
#include <oc/time/Time.hpp>
#include <oc/type/Event.hpp>

#include "config/Timing.hpp"
#include "config/TimeCompat.hpp"
#include "app/ExtmemAllocator.hpp"
#include "sequencer/MidiCcGlobalFrameCoordinator.hpp"
#include "sequencer/RealtimeMidiQueue.hpp"
#include "sequencer/SequencerCcLaneRuntime.hpp"
#include "sequencer/SequencerInternalTimerLane.hpp"
#include "sequencer/SequencerPlaybackService.hpp"

namespace core::sequencer {

class SequencerRealtimeLane {
public:
    SequencerRealtimeLane(core::state::sequencer::SequencerState& sequencer,
                          core::state::StatusBarState& statusBar,
                          oc::api::MidiAPI& midi,
                          SequencerRuntimeSnapshotBank& snapshotBank,
                          const SequencerRuntimeGraphBank& graphBank,
                          core::state::sequencer::SequencerTrackActivationQueue&
                              trackActivations)
        : ccLaneRuntime(core::app::makeExtmemUnique<SequencerCcLaneRuntime>())
        , ccPredictiveLaneRuntime(
              core::app::makeExtmemUnique<SequencerCcLaneRuntime>()
          )
        , ccCoordinator(
              core::app::makeExtmemUnique<MidiCcGlobalFrameCoordinator>(
                  midiQueue
              )
          )
        , playback(sequencer,
                   statusBar,
                   midiQueue,
                   graphBank,
                   &trackActivations,
                   ccLaneRuntime.get(),
                   ccCoordinator.get(),
                   ccPredictiveLaneRuntime.get())
        , timer(
              midi,
              midiQueue,
              snapshotBank,
              projectTrackSnapshots,
              playback
          ) {}

    [[nodiscard]] bool valid() const {
        return ccLaneRuntime != nullptr &&
               ccPredictiveLaneRuntime != nullptr &&
               ccCoordinator != nullptr &&
               playback.ccTemporalScratchValid();
    }

    MidiCcGlobalFrameCoordinator* coordinator() const {
        return ccCoordinator.get();
    }

    RealtimeMidiQueue midiQueue{};
    ProjectTrackRuntimeSnapshotBank projectTrackSnapshots{};
    core::app::ExtmemUniquePtr<SequencerCcLaneRuntime> ccLaneRuntime;
    core::app::ExtmemUniquePtr<SequencerCcLaneRuntime>
        ccPredictiveLaneRuntime;
    core::app::ExtmemUniquePtr<MidiCcGlobalFrameCoordinator> ccCoordinator;
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
    , project_track_state_(state.projectTracks)
    , status_bar_state_(state.statusBar)
    , midi_sync_state_(state.midiSync)
    , track_activations_(state.trackActivations)
    , cc_coordinator_publication_(state.ccCoordinatorPublication)
    , runtime_project_revision_(state.runtimeProjectRevision)
    , consumed_runtime_project_revision_(
          state.runtimeProjectRevision != nullptr
              ? state.runtimeProjectRevision->get()
              : 0
      )
    , midi_clock_sync_(midi)
    , snapshot_bank_(state.sequencer, state.trackBank, state.projectNavigation)
    , realtime_lane_(new (std::nothrow) SequencerRealtimeLane(
          state.sequencer,
          state.statusBar,
          midi,
          snapshot_bank_,
          runtime_graph_bank_,
          state.trackActivations
      )) {
    if (!realtime_lane_ || !realtime_lane_->valid()) {
        failSequencerRuntimeAllocation();
    }
    if (cc_coordinator_publication_ != nullptr) {
        *cc_coordinator_publication_ = realtime_lane_->coordinator();
    }
    // Initial flat state remains useful even if PSRAM is unavailable: root
    // steps can still play and the graph bank will retry on later updates.
    (void)runtime_graph_bank_.prepare(sequencer_state_, track_bank_state_);
    const uint8_t initialSnapshotIndex = snapshot_bank_.refresh();
    if (snapshot_bank_.lastRefreshSucceeded()) {
        (void)realtime_lane_->projectTrackSnapshots.publish(
            initialSnapshotIndex,
            project_track_state_,
            snapshot_bank_.snapshot(initialSnapshotIndex).enabledMask
        );
        runtime_graph_bank_.publishPrepared([this, initialSnapshotIndex]() {
            snapshot_bank_.commit(initialSnapshotIndex);
        });
    } else {
        runtime_graph_bank_.discardPrepared();
    }
    subscribeToMidiEvents_();
}

FLASHMEM SequencerRuntimeService::~SequencerRuntimeService() {
    stop();
    unsubscribeFromMidiEvents_();
    if (cc_coordinator_publication_ != nullptr &&
        *cc_coordinator_publication_ == realtime_lane_->coordinator()) {
        *cc_coordinator_publication_ = nullptr;
    }
}

void SequencerRuntimeService::update() {
    OC_PERF_SCOPE(perfRuntime, "sequencer.runtime");
    const bool projectRuntimeReset = consumeProjectRuntimeReset_();
    const uint32_t nowUs = core::time_compat::micros();
    const uint32_t nowMs = oc::time::millis();
    const auto clockConfig = captureClockSyncRuntimeConfig_();
    const auto activationPublication =
        track_activations_.captureRuntimePublication();

    ClockDomainUpdateResult clockDomain{};
    {
        OC_PERF_SCOPE(perfClock, "sequencer.clock-domain");
        clockDomain = updateClockDomainOwnership_(clockConfig, nowMs);
    }

    const bool resyncRequested = midi_clock_sync_.consumeResyncRequest();
    bool runtimePublicationDue = true;
#ifdef ARDUINO
    runtimePublicationDue = runtimePublicationDue_(
        nowUs,
        projectRuntimeReset || resyncRequested || !activationPublication.empty()
    );
#endif
    bool graphGenerationReady = false;
    uint8_t snapshotIndex = snapshot_bank_.activeIndex();
    if (runtimePublicationDue) {
        graphGenerationReady =
            runtime_graph_bank_.prepare(sequencer_state_, track_bank_state_);
        // Keep graph and flat data on the same published generation. On a rare
        // PSRAM allocation failure, retain the previous pair and retry later.
        if (graphGenerationReady) snapshotIndex = snapshot_bank_.refresh();
        if (graphGenerationReady && !snapshot_bank_.lastRefreshSucceeded()) {
            runtime_graph_bank_.discardPrepared();
            graphGenerationReady = false;
            snapshotIndex = snapshot_bank_.activeIndex();
        }
    }
    const auto& runtimeSnapshot = snapshot_bank_.snapshot(snapshotIndex);
    if (graphGenerationReady) {
        (void)realtime_lane_->projectTrackSnapshots.publish(
            snapshotIndex,
            project_track_state_,
            runtimeSnapshot.enabledMask
        );
    }

    bool usingInternalTimerPath = false;

#ifdef ARDUINO
    if (clockDomain.timerOwnsTransport) {
        if (graphGenerationReady) {
            runtime_graph_bank_.publishPrepared([
                this,
                &clockConfig,
                snapshotIndex,
                &activationPublication
            ]() {
                realtime_lane_->timer.publishRealtimeInputs(
                    clockConfig,
                    snapshotIndex
                );
                track_activations_.applyRuntimePublication(activationPublication);
            });
        }

        if (resyncRequested) {
            realtime_lane_->timer.stop();
            stopPlayback_();
        }

        usingInternalTimerPath = realtime_lane_->timer.start();
        if (!usingInternalTimerPath) {
            OC_LOG_WARN("{}", "[SequencerRuntime] failed to start internal playback timer");
            midi_clock_sync_.update(clockConfig, nowMs, true);
            clockDomain.transport = midi_clock_sync_.transportSnapshot();
        }
    } else {
        realtime_lane_->timer.stop();
        if (graphGenerationReady) {
            runtime_graph_bank_.publishPrepared([
                this,
                snapshotIndex,
                &activationPublication
            ]() {
                snapshot_bank_.commit(snapshotIndex);
                track_activations_.applyRuntimePublication(activationPublication);
            });
        }
    }
#endif

#ifndef ARDUINO
    if (graphGenerationReady) {
        runtime_graph_bank_.publishPrepared([
            this,
            snapshotIndex,
            &activationPublication
        ]() {
            snapshot_bank_.commit(snapshotIndex);
            track_activations_.applyRuntimePublication(activationPublication);
        });
    }
#endif

    if (usingInternalTimerPath) {
        // Runtime activation acknowledgements are transactional, not visual;
        // keep them responsive at the app cadence. The larger telemetry/UI
        // copy cannot be consumed faster than LVGL's service cadence.
        track_activations_.publishRealtimeTelemetry();
        if (uiProjectionDue_(nowUs)) {
            OC_PERF_SCOPE(perfTimerUi, "sequencer.timer-ui-projection");
            publishPlaybackUiFromTimerPath_(nowMs);
        }
    } else {
        if (resyncRequested) {
            stopPlayback_();
        }

        // snapshotIndex is the committed 0/1 runtime-bank index used by both
        // publications, so the canonical Track projection is guaranteed.
        const auto& projectTracks =
            *realtime_lane_->projectTrackSnapshots.snapshot(snapshotIndex);
        realtime_lane_->playback.update(
            runtimeSnapshot,
            clockDomain.transport.tick,
            clockDomain.transport.playing,
            nowUs,
            clockDomain.transport.tickPeriodUs,
            projectTracks,
            true,
            snapshot_bank_.laneSnapshot(snapshotIndex),
            !clockDomain.transport.usingExternalSource
            , snapshot_bank_.drumSnapshot(snapshotIndex)
        );
        track_activations_.publishRealtimeTelemetry();
        drainRealtimeMidiQueue_(core::time_compat::micros());
        realtime_lane_->playback.publishUiState(nowMs);
    }

    applyMidiClockSyncUiProjection(
        status_bar_state_,
        midi_sync_state_,
        midi_clock_sync_.takeUiProjectionSnapshot(),
        nowMs
    );
    OC_PERF_UNITS(
        perfRuntime,
        runtimePublicationDue ? 1U : 0U,
        activationPublication.empty() ? 0U : 1U
    );
}

bool SequencerRuntimeService::consumeProjectRuntimeReset_() {
    if (runtime_project_revision_ == nullptr) return false;
    const uint32_t revision = runtime_project_revision_->get();
    if (revision == consumed_runtime_project_revision_) return false;
#ifdef ARDUINO
    realtime_lane_->timer.stop();
#endif
    stopPlayback_();
    realtime_lane_->playback.resetCcProject();
    consumed_runtime_project_revision_ = revision;
    return true;
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

SequencerRuntimeService::ClockDomainUpdateResult
SequencerRuntimeService::updateClockDomainOwnership_(
    const MidiClockSyncRuntimeConfig& config,
    uint32_t nowMs
) {
#ifdef ARDUINO
    const auto previousTransport = midi_clock_sync_.transportSnapshot();
    const bool predictedTimerOwnsTransport =
        config.mode != core::state::MidiSyncMode::SLAVE &&
        !previousTransport.usingExternalSource;

    midi_clock_sync_.update(config, nowMs, !predictedTimerOwnsTransport);

    auto transport = midi_clock_sync_.transportSnapshot();
    const bool actualTimerOwnsTransport =
        config.mode != core::state::MidiSyncMode::SLAVE &&
        !transport.usingExternalSource;

    if (actualTimerOwnsTransport != predictedTimerOwnsTransport) {
        midi_clock_sync_.update(config, nowMs, !actualTimerOwnsTransport);
        transport = midi_clock_sync_.transportSnapshot();
    }

    return {
        .timerOwnsTransport = actualTimerOwnsTransport,
        .transport = transport,
    };
#else
    midi_clock_sync_.update(config, nowMs);
    return {
        .timerOwnsTransport = false,
        .transport = midi_clock_sync_.transportSnapshot(),
    };
#endif
}

bool SequencerRuntimeService::runtimePublicationDue_(
    uint32_t nowUs,
    bool force
) {
    if (!force && runtime_publication_started_ &&
        static_cast<uint32_t>(nowUs - last_runtime_publication_us_) <
            Config::Timing::SEQUENCER_AUTHORING_PUBLICATION_PERIOD_US) {
        return false;
    }
    last_runtime_publication_us_ = nowUs;
    runtime_publication_started_ = true;
    return true;
}

bool SequencerRuntimeService::uiProjectionDue_(uint32_t nowUs) {
    return ui_projection_deadline_.consumeIfDue(nowUs);
}

// The timer lane has already produced a bounded snapshot under lock. Mapping
// that snapshot into observable UI signals is main-loop control-plane work.
FLASHMEM void SequencerRuntimeService::publishPlaybackUiFromTimerPath_(
    uint32_t nowMs
) {
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

FLASHMEM void SequencerRuntimeService::drainRealtimeMidiQueueFully_(
    uint32_t nowUs
) {
    realtime_lane_->midiQueue.drainDue(midi_, nowUs, UINT32_MAX);
    midi_.serviceOutput(UINT32_MAX);
}

FLASHMEM void SequencerRuntimeService::stopPlayback_() {
    // Future events no longer belong to the stopped generation. Drop them,
    // then drain each track's immediate panic note-offs before the next track
    // can fill the bounded realtime queue.
    realtime_lane_->midiQueue.clear();
    realtime_lane_->playback.markCcTransportStopped();
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

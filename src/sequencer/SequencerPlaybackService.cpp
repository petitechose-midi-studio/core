#include "SequencerPlaybackService.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "config/TimeCompat.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerTrackBankOps.hpp"

namespace core::sequencer {

void SequencerPlaybackService::PendingNoteActivity::reset() {
    noteOutActive = false;
    noteOnCount = 0;
    noteOffCount = 0;
    panicNoteOffCount = 0;
    queuedEventCount = 0;
    trackVelocity.fill(0);
}

void SequencerPlaybackService::PendingNoteActivity::recordNoteOn(uint8_t trackIndex,
                                                                 uint8_t velocity) {
    noteOutActive = true;
    noteOnCount += 1;
    queuedEventCount += 1;
    if (trackIndex >= TRACK_COUNT) {
        return;
    }
    trackVelocity[trackIndex] = std::max(trackVelocity[trackIndex], velocity);
}

void SequencerPlaybackService::PendingNoteActivity::recordNoteOff() {
    noteOffCount += 1;
    queuedEventCount += 1;
}

void SequencerPlaybackService::PendingNoteActivity::recordPanicNoteOffs(uint32_t count) {
    panicNoteOffCount += count;
    queuedEventCount += count;
}

SequencerPlaybackActivitySnapshot SequencerPlaybackService::PendingNoteActivity::snapshot() const {
    return {
        .noteOnCount = noteOnCount,
        .noteOffCount = noteOffCount,
        .panicNoteOffCount = panicNoteOffCount,
        .queuedEventCount = queuedEventCount,
    };
}

SequencerPlaybackService::PendingNoteActivityObserver::PendingNoteActivityObserver(
    PendingNoteActivity& pendingNoteActivity
)
    : pending_note_activity_(pendingNoteActivity) {}

void SequencerPlaybackService::PendingNoteActivityObserver::onNoteOn(uint8_t trackIndex,
                                                                     uint8_t velocity) {
    pending_note_activity_.recordNoteOn(trackIndex, velocity);
}

void SequencerPlaybackService::PendingNoteActivityObserver::onNoteOff() {
    pending_note_activity_.recordNoteOff();
}

void SequencerPlaybackService::PendingNoteActivityObserver::onPanicNoteOffs(uint32_t count) {
    pending_note_activity_.recordPanicNoteOffs(count);
}

void SequencerPlaybackService::PendingUiProjection::reset() {
    noteOutPulse = false;
    beatPulse = false;
    trackVelocity.fill(0);
}

void SequencerPlaybackService::handleActiveTrackSwitch_() {
    const uint8_t activeTrack = runtime_active_track_;
    if (activeTrack == last_active_track_) {
        return;
    }

    last_playhead_ = activeRuntimeState_().playheadStep;
    last_active_track_ = activeTrack;
}

FLASHMEM SequencerPlaybackService::SequencerPlaybackService(
    core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::SequencerTrackBankState& trackBank,
    core::state::StatusBarState& statusBar,
    RealtimeMidiQueue& midiQueue
)
    : sequencer_(sequencer)
    , track_bank_(trackBank)
    , status_bar_(statusBar)
    , track_runtime_states_(
          std::make_unique<oc::note::sequencer::StepSequencerRuntimeState[]>(TRACK_COUNT)
      )
{
    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        track_event_sinks_[i] =
            std::make_unique<SequencerMidiEventSink>(midiQueue, i, &note_activity_observer_);
        track_engines_[i] =
            std::make_unique<oc::note::sequencer::StepSequencerEngine>(
                track_runtime_states_[i],
                *track_event_sinks_[i]
            );
    }
    last_active_track_ = trackBank.activeTrackIndex();
    runtime_active_track_ = last_active_track_;
    runtime_enabled_mask_ = trackBank.currentEnabledMask();
    auto snapshot = core::app::makeExtmemUnique<
        core::state::sequencer::SequencerTrackBankSnapshot
    >();
    if (snapshot) {
        core::state::sequencer::captureTrackBankSnapshot(trackBank, sequencer, *snapshot);
        syncRuntimeStates_(*snapshot);
    }
    publishRuntimeTelemetry(sequencer_, activeRuntimeState_());
}

void SequencerPlaybackService::update(const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
                                      uint32_t tick,
                                      bool playing,
                                      uint32_t nowMs,
                                      uint32_t nowUs,
                                      uint32_t tickPeriodUs,
                                      bool publishRuntimeState,
                                      bool emitLogs) {
    const uint32_t startUs = core::time_compat::micros();
    pending_note_activity_.reset();
    pending_ui_projection_.reset();
    syncRuntimeStates_(snapshot);

    handleActiveTrackSwitch_();
    if (!playing) {
        for (auto& sink : track_event_sinks_) {
            if (sink) {
                sink->setTimeline(tick, nowUs, tickPeriodUs);
            }
        }
        for (auto& trackEngine : track_engines_) {
            if (trackEngine) {
                trackEngine->update(tick, false);
            }
        }
        last_playhead_ = -1;
        if (publishRuntimeState) {
            publishRuntimeTelemetry(sequencer_, activeRuntimeState_());
        }
        const uint32_t updateUs = core::time_compat::micros() - startUs;
        recordProfiling_(tick, false, updateUs, nowMs);
        if (emitLogs) {
            profiler_.maybeLog(nowMs);
        }
        return;
    }

    for (uint8_t i = 0; i < track_engines_.size(); ++i) {
        auto& trackEngine = track_engines_[i];
        if (!trackEngine) continue;
        if (track_event_sinks_[i]) {
            track_event_sinks_[i]->setTimeline(tick, nowUs, tickPeriodUs);
        }
        trackEngine->update(
            tick,
            (runtime_enabled_mask_ & static_cast<uint16_t>(1U << i)) != 0
        );
    }

    collectUiProjection_();

    if (publishRuntimeState) {
        publishRuntimeTelemetry(sequencer_, activeRuntimeState_());
    }

    const auto& activeRuntime = activeRuntimeState_();
    const int16_t playhead = activeRuntime.playheadStep;
    if (playhead >= 0 && playhead != last_playhead_) {
        const uint8_t spb = activeRuntime.stepsPerBeat;
        if (spb > 0 && (static_cast<uint8_t>(playhead) % spb) == 0) {
            pending_ui_projection_.beatPulse = true;
        }
    }
    last_playhead_ = playhead;
    const uint32_t updateUs = core::time_compat::micros() - startUs;
    recordProfiling_(tick, true, updateUs, nowMs);
    if (emitLogs) {
        profiler_.maybeLog(nowMs);
    }
}

FLASHMEM void SequencerPlaybackService::stop() {
    for (auto& trackEngine : track_engines_) {
        if (trackEngine) {
            trackEngine->reset();
        }
    }
    publishRuntimeTelemetry(sequencer_, activeRuntimeState_());
    last_playhead_ = -1;
    pending_ui_projection_.reset();
}

void SequencerPlaybackService::syncRuntimeStates_(
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot
) {
    runtime_active_track_ =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(snapshot.activeTrack);
    runtime_enabled_mask_ = snapshot.enabledMask;

    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        track_runtime_states_[i].variationTelemetryEnabled = (i == runtime_active_track_);

        const auto trackSignature = captureRuntimeStateSignature(snapshot.tracks[i]);
        if (track_runtime_signatures_[i].matches(trackSignature)) {
            continue;
        }

        syncRuntimeState(track_runtime_states_[i], snapshot.tracks[i]);
        syncRuntimeGraph_(i, snapshot);
        track_runtime_signatures_[i] = trackSignature;
    }
}

void SequencerPlaybackService::syncRuntimeGraph_(
    uint8_t trackIndex,
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot
) {
    if (trackIndex >= TRACK_COUNT || !track_engines_[trackIndex]) return;

    const auto* graphSource = (trackIndex == snapshot.activeTrack)
                                  ? core::state::sequencer::graphView(sequencer_.pattern)
                                  : core::state::sequencer::graphView(track_bank_.track(trackIndex));
    if (graphSource == nullptr) {
        track_runtime_graphs_[trackIndex].reset();
        track_engines_[trackIndex]->setGraph(nullptr);
        return;
    }

    if (!track_runtime_graphs_[trackIndex]) {
        track_runtime_graphs_[trackIndex] =
            core::app::makeExtmemUnique<oc::note::sequencer::StepSequencerGraph>();
    }
    if (!track_runtime_graphs_[trackIndex]) {
        track_engines_[trackIndex]->setGraph(nullptr);
        return;
    }

    *track_runtime_graphs_[trackIndex] = *graphSource;
    track_engines_[trackIndex]->setGraph(
        track_runtime_graphs_[trackIndex]->enabled ? track_runtime_graphs_[trackIndex].get() : nullptr
    );
}

void SequencerPlaybackService::collectUiProjection_() {
    if (pending_note_activity_.noteOutActive) {
        pending_ui_projection_.noteOutPulse = true;
    }

    for (uint8_t track = 0; track < pending_note_activity_.trackVelocity.size(); ++track) {
        const uint8_t velocity = pending_note_activity_.trackVelocity[track];
        if (velocity == 0) continue;
        pending_ui_projection_.trackVelocity[track] =
            std::max(pending_ui_projection_.trackVelocity[track], velocity);
    }
}

FLASHMEM void SequencerPlaybackService::publishUiState(uint32_t nowMs) {
    publishUiProjection(takeUiProjectionSnapshot(), nowMs);
}

FLASHMEM void SequencerPlaybackService::publishUiProjection(const UiProjectionSnapshot& projection, uint32_t nowMs) {
    if (projection.noteOutPulse) {
        status_bar_.pulseNoteOut(nowMs);
    }

    if (projection.beatPulse) {
        status_bar_.pulseBeat(nowMs);
    }

    for (uint8_t track = 0; track < projection.trackVelocity.size(); ++track) {
        const uint8_t velocity = projection.trackVelocity[track];
        if (velocity == 0) continue;
        status_bar_.pulseTrackNote(track, velocity, nowMs);
    }
}

FLASHMEM SequencerPlaybackService::UiProjectionSnapshot SequencerPlaybackService::takeUiProjectionSnapshot() {
    UiProjectionSnapshot snapshot{
        .noteOutPulse = pending_ui_projection_.noteOutPulse,
        .beatPulse = pending_ui_projection_.beatPulse,
        .trackVelocity = pending_ui_projection_.trackVelocity,
    };
    pending_ui_projection_.reset();
    return snapshot;
}

const oc::note::sequencer::StepSequencerRuntimeState&
SequencerPlaybackService::activeRuntimeState_() const {
    return track_runtime_states_[runtime_active_track_];
}

SequencerRuntimeTelemetrySnapshot SequencerPlaybackService::copyActiveRuntimeTelemetry() const {
    return captureRuntimeTelemetry(activeRuntimeState_());
}

FLASHMEM bool SequencerPlaybackService::takeProfilingSnapshot(uint32_t nowMs, ProfilingSnapshot& snapshot) {
    return profiler_.takeSnapshot(nowMs, snapshot);
}

void SequencerPlaybackService::recordProfiling_(uint32_t tick,
                                                bool playing,
                                                uint32_t updateUs,
                                                uint32_t nowMs) {
    profiler_.record(tick, playing, updateUs, pending_note_activity_.snapshot(), nowMs);
}

}  // namespace core::sequencer

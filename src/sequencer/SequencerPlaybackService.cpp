#include "SequencerPlaybackService.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>

namespace core::sequencer {

SequencerPlaybackService::PendingNoteActivityObserver::PendingNoteActivityObserver(
    PendingUiProjection& pendingUiProjection
)
    : pending_ui_projection_(pendingUiProjection) {}

void SequencerPlaybackService::PendingNoteActivityObserver::onNoteOn(uint8_t trackIndex,
                                                                     uint8_t velocity) {
    pending_ui_projection_.recordNoteOn(trackIndex, velocity);
}

void SequencerPlaybackService::PendingUiProjection::reset() {
    noteOutPulse = false;
    beatPulse = false;
    trackVelocity.fill(0);
}

void SequencerPlaybackService::PendingUiProjection::recordNoteOn(uint8_t trackIndex,
                                                                 uint8_t velocity) {
    noteOutPulse = true;
    if (trackIndex >= TRACK_COUNT) return;
    trackVelocity[trackIndex] = std::max(trackVelocity[trackIndex], velocity);
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
    core::state::StatusBarState& statusBar,
    RealtimeMidiQueue& midiQueue,
    const SequencerRuntimeGraphBank& runtimeGraphBank
)
    : sequencer_(sequencer)
    , status_bar_(statusBar)
    , runtime_graph_bank_(runtimeGraphBank)
{
    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        track_event_sinks_[i].emplace(midiQueue, i, &note_activity_observer_);
        track_engines_[i].emplace(track_runtime_states_[i], *track_event_sinks_[i]);
    }
    publishRuntimeTelemetry(sequencer_, activeRuntimeState_());
}

void SequencerPlaybackService::update(
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
    uint32_t tick,
                                      bool playing,
                                      uint32_t nowUs,
                                      uint32_t tickPeriodUs,
                                      bool publishRuntimeState) {
    OC_PERF_SCOPE(perfPlayback, "sequencer.playback");
    OC_PERF_UNITS(perfPlayback, playing ? 1U : 0U, 0);
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
        return;
    }

    for (uint8_t i = 0; i < track_engines_.size(); ++i) {
        auto& trackEngine = track_engines_[i];
        if (!trackEngine) continue;
        if (track_event_sinks_[i]) {
            track_event_sinks_[i]->setTimeline(tick, nowUs, tickPeriodUs);
        }
        const uint16_t trackBit = static_cast<uint16_t>(1U << i);
        trackEngine->update(
            tick,
            (runtime_enabled_mask_ & trackBit) != 0 &&
            (runtime_muted_mask_ & trackBit) == 0
        );
    }

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
}

FLASHMEM void SequencerPlaybackService::stopTrack(uint8_t trackIndex) {
    if (trackIndex >= track_engines_.size() || !track_engines_[trackIndex]) return;
    track_engines_[trackIndex]->reset();
}

FLASHMEM void SequencerPlaybackService::completeStop() {
    publishRuntimeTelemetry(sequencer_, activeRuntimeState_());
    last_playhead_ = -1;
    pending_ui_projection_.reset();
}

void SequencerPlaybackService::syncRuntimeStates_(
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot
) {
    runtime_active_track_ =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            snapshot.activeTrack
        );
    runtime_enabled_mask_ = snapshot.enabledMask;
    runtime_muted_mask_ = snapshot.mutedMask;

    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        track_runtime_states_[i].variationTelemetryEnabled = (i == runtime_active_track_);
        if (track_engines_[i]) {
            track_engines_[i]->setGraph(runtime_graph_bank_.graphForTrack(i));
        }

        const auto trackSignature = captureRuntimeStateSignature(snapshot.tracks[i]);
        if (track_runtime_signatures_[i].matches(trackSignature)) {
            continue;
        }

        syncRuntimeState(track_runtime_states_[i], snapshot.tracks[i]);
        track_runtime_signatures_[i] = trackSignature;
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

}  // namespace core::sequencer

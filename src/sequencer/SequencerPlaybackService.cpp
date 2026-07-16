#include "SequencerPlaybackService.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/note/clock/ClockConstants.hpp>

#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "sequencer/SequencerRuntimeSnapshotBank.hpp"

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
    ccOutPulse = false;
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
    const SequencerRuntimeGraphBank& runtimeGraphBank,
    core::state::sequencer::SequencerTrackActivationQueue* trackActivations,
    SequencerCcLaneRuntime* ccLaneRuntime,
    core::handler::MidiCcGlobalFrameCoordinator* ccCoordinator
)
    : sequencer_(sequencer)
    , status_bar_(statusBar)
    , midi_queue_(midiQueue)
    , runtime_graph_bank_(runtimeGraphBank)
    , track_activations_(trackActivations)
    , cc_lane_runtime_(ccLaneRuntime)
    , cc_coordinator_(ccCoordinator)
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
                                       bool publishRuntimeState,
                                       const SequencerCcLaneRuntimeProjectSnapshot* ccLaneSnapshot) {
    OC_PERF_SCOPE(perfPlayback, "sequencer.playback");
    OC_PERF_UNITS(perfPlayback, playing ? 1U : 0U, 0);
    for (auto& sink : track_event_sinks_) {
        if (sink) {
            sink->setTimeline(tick, nowUs, tickPeriodUs);
        }
    }
    if (cc_coordinator_ != nullptr) {
        cc_coordinator_->publishProjectControlClock(
            tick,
            playing,
            nowUs,
            tickPeriodUs
        );
    }
    syncRuntimeStates_(snapshot, tick, playing);
    // Publish and arbitrate CC before note engines enqueue the same-deadline
    // events. RealtimeMidiQueue additionally enforces Off < CC < On ordering.
    processCcRuntime_(snapshot, ccLaneSnapshot, tick, playing, nowUs);

    handleActiveTrackSwitch_();
    if (!playing) {
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
        const uint16_t trackBit = static_cast<uint16_t>(1U << i);
        trackEngine->update(
            tick,
            (runtime_enabled_mask_ & trackBit) != 0 &&
            (runtime_muted_mask_ & trackBit) == 0 &&
            track_runtime_states_[i].midiChannel <= 15U
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

void SequencerPlaybackService::markCcTransportStopped() {
    cc_transport_playing_ = false;
    if (cc_coordinator_ != nullptr) {
        cc_coordinator_->discardPendingRetryForTransportStop();
    }
}

void SequencerPlaybackService::resetCcProject() {
    if (cc_lane_runtime_ != nullptr) {
        cc_lane_runtime_->resetProject();
    }
    if (cc_coordinator_ != nullptr) {
        cc_coordinator_->resetProject();
    }
    last_cc_tick_ = 0;
    cc_transport_playing_ = false;
}

uint8_t SequencerPlaybackService::ccTicksPerStep_(
    const core::state::sequencer::SequencerPatternSnapshot& pattern
) {
    uint8_t stepsPerBeat = pattern.stepsPerBeat;
    if (stepsPerBeat == 0) {
        stepsPerBeat =
            oc::note::sequencer::StepSequencerRuntimeState::DEFAULT_STEPS_PER_BEAT;
    }
    if (stepsPerBeat > oc::note::clock::PPQN) {
        stepsPerBeat = static_cast<uint8_t>(oc::note::clock::PPQN);
    }
    const uint8_t result = static_cast<uint8_t>(
        oc::note::clock::PPQN / stepsPerBeat
    );
    return result == 0 ? 1U : result;
}

void SequencerPlaybackService::processCcRuntime_(
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
    const SequencerCcLaneRuntimeProjectSnapshot* laneSnapshot,
    uint32_t tick,
    bool playing,
    uint32_t nowUs
) {
    if (cc_lane_runtime_ == nullptr || cc_coordinator_ == nullptr) return;

    const bool musicalTickAdvanced = playing &&
        (!cc_transport_playing_ || tick != last_cc_tick_);
    if (!playing && cc_transport_playing_) {
        // Transport is a scheduler gate, not a new CC author frame. Retain
        // the published lane holds so lower-priority Macro authors cannot
        // become winners merely because playback stopped.
        cc_transport_playing_ = false;
    }
    if (musicalTickAdvanced) {
        SequencerCcLaneRuntime::Inputs inputs{};
        if (playing) {
            for (uint8_t track = 0; track < inputs.size(); ++track) {
                const auto& pattern = snapshot.tracks[track];
                const uint8_t ticksPerStep = ccTicksPerStep_(pattern);
                const uint8_t length = std::clamp<uint8_t>(
                    pattern.length,
                    1U,
                    core::state::sequencer::SequencerPatternState::MAX_STEPS
                );
                const auto activation = track_activations_ != nullptr
                    ? track_activations_->realtimeView(track)
                    : core::state::sequencer::SequencerTrackActivationRealtimeView{};
                inputs[track] = {
                    .lanes = laneSnapshot ? laneSnapshot->lanesForTrack(track) : nullptr,
                    .route = core::state::sequencer::makeSequencerCcTrackRoute(
                        core::handler::MidiCcGlobalFrameCoordinator::OUTPUT_PORT,
                        pattern.midiChannel
                    ),
                    .step = static_cast<uint8_t>((tick / ticksPerStep) % length),
                    .patternLength = length,
                    .tickInStep = static_cast<uint8_t>(tick % ticksPerStep),
                    .ticksPerStep = ticksPerStep,
                    .enabled = (snapshot.enabledMask & static_cast<uint16_t>(1U << track)) != 0,
                    .muted = (snapshot.mutedMask & static_cast<uint16_t>(1U << track)) != 0,
                    .stepTriggered = (tick % ticksPerStep) == 0,
                    .frozen = activation.disposition !=
                        core::state::sequencer::SequencerTrackActivationRealtimeView::
                            Disposition::NORMAL,
                };
            }
        }

        SequencerCcLaneRuntimeFrame frame{};
        if (cc_lane_runtime_->buildMusicalTickFrame(inputs, playing, frame) ==
            SequencerCcLaneRuntimeStatus::OK) {
            (void)cc_coordinator_->publishSequencerLanes(frame);
        }
        last_cc_tick_ = tick;
        cc_transport_playing_ = playing;
    }

    // Retrying a rejected atomic queue batch and consuming Macro publications
    // do not re-evaluate lane state between musical ticks.
    if (cc_coordinator_->needsLiveResolution()) {
        const auto result = cc_coordinator_->resolveLive(nowUs);
        if (result.queuedEmissionCount > 0) {
            pending_ui_projection_.ccOutPulse = true;
        }
    }
}

void SequencerPlaybackService::syncRuntimeStates_(
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
    uint32_t tick,
    bool playing
) {
    runtime_active_track_ =
        core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
            snapshot.activeTrack
        );

    for (uint8_t i = 0; i < TRACK_COUNT; ++i) {
        track_runtime_states_[i].variationTelemetryEnabled = (i == runtime_active_track_);
        if (track_activations_ != nullptr) {
            const auto activation = track_activations_->realtimeView(i);
            if (activation.disposition ==
                core::state::sequencer::SequencerTrackActivationRealtimeView::Disposition::FROZEN) {
                continue;
            }
            if (activation.disposition ==
                core::state::sequencer::SequencerTrackActivationRealtimeView::Disposition::STAGED) {
                if (playing && activation.requiresLocalLoopBoundary &&
                    !isLocalLoopBoundary_(i, tick)) {
                    continue;
                }
                applyStagedTrack_(snapshot, i, activation.generation, tick, playing);
                continue;
            }
        }

        syncRuntimeMasksForTrack_(snapshot, i);
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

bool SequencerPlaybackService::isLocalLoopBoundary_(uint8_t trackIndex,
                                                     uint32_t tick) const {
    if (trackIndex >= TRACK_COUNT) return true;
    const auto& runtime = track_runtime_states_[trackIndex];
    const uint8_t length = runtime.patternLength();
    if (length == 0) return true;

    uint8_t stepsPerBeat = runtime.stepsPerBeat;
    if (stepsPerBeat == 0) {
        stepsPerBeat =
            oc::note::sequencer::StepSequencerRuntimeState::DEFAULT_STEPS_PER_BEAT;
    }
    if (stepsPerBeat > oc::note::clock::PPQN) {
        stepsPerBeat = static_cast<uint8_t>(oc::note::clock::PPQN);
    }
    uint8_t ticksPerStep = static_cast<uint8_t>(
        oc::note::clock::PPQN / stepsPerBeat
    );
    if (ticksPerStep == 0) ticksPerStep = 1;
    const uint32_t loopTicks =
        static_cast<uint32_t>(length) * static_cast<uint32_t>(ticksPerStep);
    return loopTicks == 0 || (tick % loopTicks) == 0;
}

void SequencerPlaybackService::syncRuntimeMasksForTrack_(
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
    uint8_t trackIndex
) {
    const uint16_t bit = static_cast<uint16_t>(1U << trackIndex);
    runtime_enabled_mask_ = (snapshot.enabledMask & bit) != 0
        ? static_cast<uint16_t>(runtime_enabled_mask_ | bit)
        : static_cast<uint16_t>(runtime_enabled_mask_ & static_cast<uint16_t>(~bit));
    runtime_muted_mask_ = (snapshot.mutedMask & bit) != 0
        ? static_cast<uint16_t>(runtime_muted_mask_ | bit)
        : static_cast<uint16_t>(runtime_muted_mask_ & static_cast<uint16_t>(~bit));
}

void SequencerPlaybackService::applyStagedTrack_(
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
    uint8_t trackIndex,
    uint32_t generation,
    uint32_t tick,
    bool playing
) {
    if (trackIndex >= TRACK_COUNT || !track_engines_[trackIndex] ||
        track_activations_ == nullptr) {
        return;
    }

    auto& engine = *track_engines_[trackIndex];
    midi_queue_.cancelPendingEvents(trackIndex);
    engine.reset();

    syncRuntimeMasksForTrack_(snapshot, trackIndex);
    syncRuntimeState(track_runtime_states_[trackIndex], snapshot.tracks[trackIndex]);
    track_runtime_signatures_[trackIndex] =
        captureRuntimeStateSignature(snapshot.tracks[trackIndex]);
    engine.setGraph(runtime_graph_bank_.graphForTrack(trackIndex));

    const uint16_t bit = static_cast<uint16_t>(1U << trackIndex);
    const bool trackPlaying = playing &&
        (runtime_enabled_mask_ & bit) != 0 &&
        (runtime_muted_mask_ & bit) == 0 &&
        track_runtime_states_[trackIndex].midiChannel <= 15U;
    if (trackPlaying) {
        if (tick == 0) {
            engine.update(0, true);
        } else {
            // Seed one tick before the exact boundary so the new generation's
            // first boundary step is scheduled, without replaying past events.
            engine.resyncToTick(tick - 1U);
        }
    }
    track_activations_->markAppliedFromRealtime(trackIndex, generation);
}

FLASHMEM void SequencerPlaybackService::publishUiState(uint32_t nowMs) {
    publishUiProjection(takeUiProjectionSnapshot(), nowMs);
}

FLASHMEM void SequencerPlaybackService::publishUiProjection(const UiProjectionSnapshot& projection, uint32_t nowMs) {
    if (projection.noteOutPulse) {
        status_bar_.pulseNoteOut(nowMs);
    }

    if (projection.ccOutPulse) {
        status_bar_.pulseCcOut(nowMs);
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
        .ccOutPulse = pending_ui_projection_.ccOutPulse,
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

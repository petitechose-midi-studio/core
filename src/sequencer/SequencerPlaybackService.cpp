#include "SequencerPlaybackService.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/note/clock/ClockConstants.hpp>

#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"
#include "sequencer/ProjectTrackRuntimeSnapshotBank.hpp"
#include "sequencer/SequencerRuntimeSnapshotBank.hpp"

namespace core::sequencer {

namespace {

uint8_t projectTrackChannel(
    const ProjectTrackRuntimeSnapshot& projectTracks,
    uint8_t track
) {
    return projectTracks.midiChannels[track];
}

int16_t projectTrackDelayMs(
    const ProjectTrackRuntimeSnapshot& projectTracks,
    uint8_t track
) {
    return projectTracks.delayMs[track];
}

uint16_t projectTrackEnabledMask(
    const ProjectTrackRuntimeSnapshot& projectTracks
) {
    return projectTracks.enabledMask;
}

uint16_t projectTrackAudibleMask(
    const ProjectTrackRuntimeSnapshot& projectTracks
) {
    return projectTracks.audibleMask;
}

int32_t projectTrackDeadlineOffsetUs(
    const ProjectTrackRuntimeSnapshot& projectTracks,
    uint8_t track,
    uint32_t tickPeriodUs,
    bool allowPredictiveLookahead
) {
    const int16_t delayMs = projectTrackDelayMs(projectTracks, track);
    if (delayMs < 0 &&
        (!allowPredictiveLookahead || tickPeriodUs == 0U)) {
        return 0;
    }
    return static_cast<int32_t>(delayMs) * 1000;
}

uint32_t emissionHorizonTick(
    uint32_t tick,
    int32_t deadlineOffsetUs,
    uint32_t tickPeriodUs
) {
    if (deadlineOffsetUs >= 0 || tickPeriodUs == 0U) return tick;
    const uint32_t leadUs = static_cast<uint32_t>(-deadlineOffsetUs);
    const uint32_t leadTicks = static_cast<uint32_t>(
        (static_cast<uint64_t>(leadUs) + tickPeriodUs - 1U) /
        tickPeriodUs
    );
    // Musical transport counters are explicitly modulo 2^32. Saturating here
    // would pin the lookahead at UINT32_MAX and break the next wrapped tick.
    return tick + leadTicks;
}

}  // namespace

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
    core::handler::MidiCcGlobalFrameCoordinator* ccCoordinator,
    SequencerCcLaneRuntime* ccPredictiveLaneRuntime
)
    : sequencer_(sequencer)
    , status_bar_(statusBar)
    , midi_queue_(midiQueue)
    , runtime_graph_bank_(runtimeGraphBank)
    , track_activations_(trackActivations)
    , cc_lane_runtime_(ccLaneRuntime)
    , cc_predictive_lane_runtime_(ccPredictiveLaneRuntime)
    , cc_coordinator_(ccCoordinator)
    , cc_temporal_scratch_(
          core::app::makeExtmemUnique<SequencerCcTemporalRuntimeScratch>()
      )
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
    const ProjectTrackRuntimeSnapshot& projectTracks,
    bool publishRuntimeState,
    const SequencerCcLaneRuntimeProjectSnapshot* ccLaneSnapshot,
    bool allowPredictiveLookahead
) {
    OC_PERF_SCOPE(perfPlayback, "sequencer.playback");
    OC_PERF_UNITS(perfPlayback, playing ? 1U : 0U, 0);
    for (uint8_t track = 0U; track < track_event_sinks_.size(); ++track) {
        auto& sink = track_event_sinks_[track];
        if (sink) {
            sink->setTimeline(
                tick,
                nowUs,
                tickPeriodUs,
                projectTrackDeadlineOffsetUs(
                    projectTracks,
                    track,
                    tickPeriodUs,
                    allowPredictiveLookahead
                )
            );
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
    reconcileProjectTracks_(
        projectTracks,
        tick,
        playing,
        nowUs,
        tickPeriodUs,
        allowPredictiveLookahead
    );
    syncRuntimeStates_(snapshot, projectTracks, tick, playing);
    // Publish and arbitrate CC before note engines enqueue the same-deadline
    // events. RealtimeMidiQueue additionally enforces Off < CC < On ordering.
    processCcRuntime_(
        snapshot,
        ccLaneSnapshot,
        projectTracks,
        tick,
        playing,
        nowUs,
        tickPeriodUs,
        allowPredictiveLookahead
    );

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
        const bool trackPlaying =
            (runtime_audible_mask_ & trackBit) != 0 &&
            track_runtime_states_[i].midiChannel <= 15U;
        const int32_t deadlineOffsetUs = projectTrackDeadlineOffsetUs(
            projectTracks,
            i,
            tickPeriodUs,
            allowPredictiveLookahead
        );
        if (!trackPlaying || deadlineOffsetUs >= 0) {
            trackEngine->update(tick, trackPlaying);
            continue;
        }

        const uint32_t horizon = emissionHorizonTick(
            tick,
            deadlineOffsetUs,
            tickPeriodUs
        );
        if (horizon < tick) {
            // The note engine's public horizon is still an ordered uint32_t
            // interval and cannot encode a future point across rollover. Leave
            // its already-planned Note edges untouched for this bounded lead
            // window. CC projection uses modular ordinals and remains exact;
            // the first post-wrap Note pass rejects the stale ordered cursor,
            // resynchronizes, and resumes normally.
            continue;
        }
        if (trackEngine->updateWithEmissionHorizon(
                tick,
                horizon,
                true
            )) {
            continue;
        }

        // Grid/region/tempo/look-ahead changes invalidate already prepared
        // future edges. Rebuild from the musical cursor, then retry exactly
        // once; the engine contract guarantees a rejected call was a no-op.
        // This is a Note-engine plan rebuild. The CC coordinator has already
        // published the same scheduler tick, so cancelling the whole Track
        // would remove a valid predictive CC and request a spurious retry.
        midi_queue_.cancelPendingNoteEvents(i);
        trackEngine->resyncToTick(tick);
        (void)trackEngine->updateWithEmissionHorizon(
            tick,
            horizon,
            true
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
    if (cc_predictive_lane_runtime_ != nullptr) {
        cc_predictive_lane_runtime_->resetProject();
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
    const ProjectTrackRuntimeSnapshot& projectTracks,
    uint32_t tick,
    bool playing,
    uint32_t nowUs,
    uint32_t tickPeriodUs,
    bool allowPredictiveLookahead
) {
    if (cc_lane_runtime_ == nullptr || cc_coordinator_ == nullptr ||
        cc_temporal_scratch_ == nullptr) return;

    const bool musicalTickAdvanced = playing &&
        (!cc_transport_playing_ || tick != last_cc_tick_);
    if (!playing && cc_transport_playing_) {
        // Transport is a scheduler gate, not a new CC author frame. Retain
        // the published lane holds so lower-priority Macro authors cannot
        // become winners merely because playback stopped.
        cc_transport_playing_ = false;
        cc_coordinator_->discardPendingRetryForTransportStop();
    }
    if (musicalTickAdvanced) {
        auto& scratch = *cc_temporal_scratch_;
        scratch.currentInputs = {};
        auto& inputs = scratch.currentInputs;
        if (playing) {
            for (uint8_t track = 0; track < inputs.size(); ++track) {
                const auto& pattern = snapshot.tracks[track];
                const uint8_t ticksPerStep = ccTicksPerStep_(pattern);
                const auto region = runtimePlaybackRegion(pattern);
                oc::note::sequencer::StepSequencerPlaybackTickPosition position{};
                const bool positionValid =
                    oc::note::sequencer::tryResolvePlaybackTick(
                        region,
                        tick,
                        ticksPerStep,
                        position
                    );
                const auto activation = track_activations_ != nullptr
                    ? track_activations_->realtimeView(track)
                    : core::state::sequencer::SequencerTrackActivationRealtimeView{};
                inputs[track] = {
                    .lanes = laneSnapshot ? laneSnapshot->lanesForTrack(track) : nullptr,
                    .route = core::state::sequencer::makeSequencerCcTrackRoute(
                        core::handler::MidiCcGlobalFrameCoordinator::OUTPUT_PORT,
                        projectTrackChannel(projectTracks, track)
                    ),
                    .step = static_cast<uint8_t>(
                        positionValid ? position.playback.stepIndex : 0U
                    ),
                    .patternLength = region.contentLength,
                    .tickInStep = static_cast<uint8_t>(
                        positionValid ? position.tickOffset : 0U
                    ),
                    .ticksPerStep = ticksPerStep,
                    .playbackOrdinal = positionValid ? position.playback.ordinal : 0,
                    .playbackRegion = region,
                    .enabled = (projectTrackEnabledMask(projectTracks) &
                                static_cast<uint16_t>(1U << track)) != 0,
                    .muted = (projectTrackAudibleMask(projectTracks) &
                              static_cast<uint16_t>(1U << track)) == 0,
                    .stepTriggered = positionValid && position.atStepBoundary,
                    .frozen = activation.disposition !=
                        core::state::sequencer::SequencerTrackActivationRealtimeView::
                            Disposition::NORMAL,
                };
            }
        }

        scratch.currentFrame = {};
        auto& currentFrame = scratch.currentFrame;
        if (cc_lane_runtime_->buildMusicalTickFrame(
                inputs,
                playing,
                currentFrame
            ) == SequencerCcLaneRuntimeStatus::OK) {
            scratch.temporalFrame = {};
            auto& temporalFrame = scratch.temporalFrame;
            temporalFrame.lifecycleGenerations =
                currentFrame.lifecycleGenerations;

            // Prepare every eligible negative-delay Track first, then seed and
            // evaluate the predictive runtime exactly once. Previously this
            // copied/reset the complete PSRAM runtime and rebuilt all 16 Tracks
            // once per negative Track (up to 16 full passes per scheduler tick).
            uint16_t preparedPredictiveTracks = 0U;
            scratch.predictiveInputs = inputs;
            auto& predictiveInputs = scratch.predictiveInputs;
            if (allowPredictiveLookahead && tickPeriodUs > 0U &&
                cc_predictive_lane_runtime_ != nullptr) {
                for (uint8_t track = 0U; track < inputs.size(); ++track) {
                    const int16_t delayMs = projectTrackDelayMs(
                        projectTracks,
                        track
                    );
                    if (delayMs >= 0 || inputs[track].frozen) continue;

                    const uint32_t advanceUs =
                        static_cast<uint32_t>(-static_cast<int32_t>(delayMs)) *
                        1000U;
                    const uint32_t leadTicks = static_cast<uint32_t>(
                        (static_cast<uint64_t>(advanceUs) + tickPeriodUs - 1U) /
                        tickPeriodUs
                    );
                    const auto& pattern = snapshot.tracks[track];
                    const uint8_t ticksPerStep = ccTicksPerStep_(pattern);
                    const auto region = runtimePlaybackRegion(pattern);
                    const uint64_t futurePhaseTicks =
                        static_cast<uint64_t>(inputs[track].tickInStep) +
                        leadTicks;
                    const uint32_t ordinalAdvance = static_cast<uint32_t>(
                        futurePhaseTicks / ticksPerStep
                    );
                    if (ordinalAdvance >
                        SequencerCcLaneRuntime::MAX_LOOKAHEAD_ORDINAL_DELTA) {
                        continue;
                    }
                    const uint32_t futureOrdinal =
                        inputs[track].playbackOrdinal + ordinalAdvance;
                    oc::note::sequencer::StepSequencerPlaybackPosition future{};
                    if (!oc::note::sequencer::tryResolvePlaybackOrdinal(
                            region,
                            futureOrdinal,
                            future
                        )) {
                        continue;
                    }

                    auto& projected = predictiveInputs[track];
                    projected.step = future.stepIndex;
                    projected.tickInStep = static_cast<uint8_t>(
                        futurePhaseTicks % ticksPerStep
                    );
                    projected.playbackOrdinal = future.ordinal;
                    projected.emissionMode =
                        SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD;
                    projected.lookaheadStartOrdinal =
                        inputs[track].playbackOrdinal;
                    projected.stepTriggered = projected.tickInStep == 0U;
                    preparedPredictiveTracks = static_cast<uint16_t>(
                        preparedPredictiveTracks |
                        static_cast<uint16_t>(1U << track)
                    );
                }
            }

            scratch.projectedFrame = {};
            auto& projectedFrame = scratch.projectedFrame;
            uint16_t projectedTracks = 0U;
            if (preparedPredictiveTracks != 0U &&
                cc_predictive_lane_runtime_->seedFrom(*cc_lane_runtime_) &&
                cc_predictive_lane_runtime_->buildMusicalTickFrame(
                    predictiveInputs,
                    true,
                    projectedFrame
                ) == SequencerCcLaneRuntimeStatus::OK) {
                projectedTracks = preparedPredictiveTracks;
            }

            for (uint8_t track = 0U; track < inputs.size(); ++track) {
                const bool projected =
                    (projectedTracks & static_cast<uint16_t>(1U << track)) != 0U;
                const auto& sourceFrame = projected ? projectedFrame : currentFrame;
                if (projected) {
                    temporalFrame.predictiveAuthorMask |=
                        UINT64_C(0x0F) <<
                        static_cast<uint8_t>(
                            track * SequencerCcLaneRuntime::LANE_COUNT
                        );
                }
                for (uint8_t index = 0U;
                     index < sourceFrame.candidateCount;
                     ++index) {
                    const auto& candidate = sourceFrame.candidates[index];
                    if (candidate.author.stableAddress /
                            SequencerCcLaneRuntime::LANE_COUNT != track) {
                        continue;
                    }
                    if (temporalFrame.candidateCount >=
                        temporalFrame.candidates.size()) {
                        temporalFrame.status =
                            SequencerCcLaneRuntimeStatus::CAPACITY_EXCEEDED;
                        break;
                    }
                    const uint8_t output = temporalFrame.candidateCount++;
                    temporalFrame.candidates[output] = candidate;
                    temporalFrame.contributions[output] =
                        sourceFrame.contributions[index];
                }
                if (!temporalFrame.ok()) break;
            }
            if (temporalFrame.ok()) {
                (void)cc_coordinator_->publishSequencerLanes(temporalFrame);
            }
        }
        last_cc_tick_ = tick;
        cc_transport_playing_ = playing;
    }

    // Retrying a rejected atomic queue batch and consuming Macro publications
    // do not re-evaluate lane state between musical ticks.
    if (cc_coordinator_->needsLiveResolution(nowUs)) {
        const auto result = cc_coordinator_->resolveLive(
            nowUs,
            projectTracks,
            tickPeriodUs,
            allowPredictiveLookahead
        );
        if (result.queuedEmissionCount > 0) {
            pending_ui_projection_.ccOutPulse = true;
        }
    }
}

void SequencerPlaybackService::syncRuntimeStates_(
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
    const ProjectTrackRuntimeSnapshot& projectTracks,
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
                applyStagedTrack_(
                    snapshot,
                    projectTracks,
                    i,
                    activation.generation,
                    tick,
                    playing
                );
                continue;
            }
        }

        syncRuntimeMasksForTrack_(projectTracks, i);
        if (track_engines_[i]) {
            track_engines_[i]->setGraph(runtime_graph_bank_.graphForTrack(i));
        }

        const auto trackSignature = captureRuntimeStateSignature(snapshot.tracks[i]);
        if (!track_runtime_signatures_[i].matches(trackSignature)) {
            const auto region = runtimePlaybackRegion(snapshot.tracks[i]);
            if (!track_engines_[i] || !track_engines_[i]->setPlaybackRegion(region)) {
                continue;
            }
            syncRuntimeState(track_runtime_states_[i], snapshot.tracks[i]);
            track_runtime_signatures_[i] = trackSignature;
        }
        track_runtime_states_[i].midiChannel =
            projectTrackChannel(projectTracks, i);

        const uint16_t bit = static_cast<uint16_t>(1U << i);
        if ((runtime_resync_mask_ & bit) != 0U) {
            if (playing && tick > 0U && track_engines_[i]) {
                track_engines_[i]->resyncToTick(tick - 1U);
            }
            runtime_resync_mask_ = static_cast<uint16_t>(
                runtime_resync_mask_ & static_cast<uint16_t>(~bit)
            );
        }
    }
}

void SequencerPlaybackService::reconcileProjectTracks_(
    const ProjectTrackRuntimeSnapshot& projectTracks,
    uint32_t tick,
    bool playing,
    uint32_t nowUs,
    uint32_t tickPeriodUs,
    bool allowPredictiveLookahead
) {
    const uint16_t nextEnabled = projectTrackEnabledMask(projectTracks);
    const uint16_t nextAudible = projectTrackAudibleMask(projectTracks);

    for (uint8_t track = 0U; track < TRACK_COUNT; ++track) {
        const uint16_t bit = static_cast<uint16_t>(1U << track);
        const uint8_t nextChannel = projectTrackChannel(projectTracks, track);
        const int16_t nextDelayMs = projectTrackDelayMs(projectTracks, track);
        const bool wasAudible = (runtime_audible_mask_ & bit) != 0U;
        const bool willBeAudible = (nextAudible & bit) != 0U;
        const bool routeChanged = runtime_track_channels_[track] != nextChannel;
        const bool delayChanged = runtime_track_delays_ms_[track] != nextDelayMs;
        const bool audibilityChanged = wasAudible != willBeAudible;
        const bool predictiveTimingChanged = nextDelayMs < 0 &&
            runtime_project_tracks_initialized_ &&
            (runtime_predictive_lookahead_ != allowPredictiveLookahead ||
             (allowPredictiveLookahead &&
              runtime_tick_period_us_ != tickPeriodUs));
        const bool resetRequired = runtime_project_tracks_initialized_ &&
            (routeChanged || delayChanged || audibilityChanged ||
             predictiveTimingChanged);

        if (resetRequired) {
            midi_queue_.cancelPendingEvents(track);
            if (cc_coordinator_ != nullptr) {
                cc_coordinator_->invalidateTrack(track);
            }
            if (track_engines_[track]) {
                // Reset uses the previous runtime Channel, so physical Note
                // Off edges are emitted on the route that originally owned
                // the notes before the canonical route is replaced below.
                track_engines_[track]->reset();
            }
            if (playing && willBeAudible) {
                runtime_resync_mask_ = static_cast<uint16_t>(
                    runtime_resync_mask_ | bit
                );
            } else {
                runtime_resync_mask_ = static_cast<uint16_t>(
                    runtime_resync_mask_ & static_cast<uint16_t>(~bit)
                );
            }
        }

        runtime_track_channels_[track] = nextChannel;
        runtime_track_delays_ms_[track] = nextDelayMs;
        track_runtime_states_[track].midiChannel = nextChannel;
    }

    runtime_enabled_mask_ = nextEnabled;
    runtime_audible_mask_ = nextAudible;
    runtime_project_tracks_initialized_ = true;
    runtime_tick_period_us_ = tickPeriodUs;
    runtime_predictive_lookahead_ = allowPredictiveLookahead;
    (void)tick;
    (void)nowUs;
}

bool SequencerPlaybackService::isLocalLoopBoundary_(uint8_t trackIndex,
                                                     uint32_t tick) const {
    if (trackIndex >= TRACK_COUNT) return true;
    const auto& runtime = track_runtime_states_[trackIndex];
    if (!track_engines_[trackIndex]) return true;

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
    oc::note::sequencer::StepSequencerPlaybackTickPosition position{};
    if (!oc::note::sequencer::tryResolvePlaybackTick(
            track_engines_[trackIndex]->playbackRegion(),
            tick,
            ticksPerStep,
            position
        )) {
        return false;
    }
    return position.atStepBoundary &&
           !position.playback.inPrelude &&
           position.playback.atLoopStart;
}

void SequencerPlaybackService::syncRuntimeMasksForTrack_(
    const ProjectTrackRuntimeSnapshot& projectTracks,
    uint8_t trackIndex
) {
    const uint16_t bit = static_cast<uint16_t>(1U << trackIndex);
    runtime_enabled_mask_ = (projectTrackEnabledMask(projectTracks) & bit) != 0
        ? static_cast<uint16_t>(runtime_enabled_mask_ | bit)
        : static_cast<uint16_t>(runtime_enabled_mask_ & static_cast<uint16_t>(~bit));
    runtime_audible_mask_ = (projectTrackAudibleMask(projectTracks) & bit) != 0
        ? static_cast<uint16_t>(runtime_audible_mask_ | bit)
        : static_cast<uint16_t>(runtime_audible_mask_ & static_cast<uint16_t>(~bit));
}

void SequencerPlaybackService::applyStagedTrack_(
    const core::state::sequencer::SequencerTrackBankSnapshot& snapshot,
    const ProjectTrackRuntimeSnapshot& projectTracks,
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

    syncRuntimeMasksForTrack_(projectTracks, trackIndex);
    const auto region = runtimePlaybackRegion(snapshot.tracks[trackIndex]);
    if (!engine.setPlaybackRegion(region)) return;
    syncRuntimeState(track_runtime_states_[trackIndex], snapshot.tracks[trackIndex]);
    track_runtime_states_[trackIndex].midiChannel =
        projectTrackChannel(projectTracks, trackIndex);
    engine.setGraph(runtime_graph_bank_.graphForTrack(trackIndex));
    track_runtime_signatures_[trackIndex] =
        captureRuntimeStateSignature(snapshot.tracks[trackIndex]);
    runtime_resync_mask_ = static_cast<uint16_t>(
        runtime_resync_mask_ &
        static_cast<uint16_t>(~static_cast<uint16_t>(1U << trackIndex))
    );

    const uint16_t bit = static_cast<uint16_t>(1U << trackIndex);
    const bool trackPlaying = playing &&
        (runtime_audible_mask_ & bit) != 0 &&
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

#include "sequencer/SequencerCcLaneRuntime.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerCcLaneProjectionOps.hpp"

namespace core::sequencer {

namespace {

using core::state::sequencer::SequencerCcLaneAddress;
using core::state::sequencer::SequencerCcLaneBank;
using core::state::shared::MidiCcCandidate;
using core::state::shared::MidiCcCandidateClass;
using core::state::shared::MidiCcDestination;
using core::state::shared::MidiCcDestinationIdentity;
using core::state::shared::MidiCcRouteValidity;

FLASHMEM bool sameIdentity(
    const MidiCcDestinationIdentity& lhs,
    const MidiCcDestinationIdentity& rhs
) {
    return core::state::shared::sameMidiCcDestinationIdentity(lhs, rhs);
}

FLASHMEM bool sameDestination(
    const MidiCcDestination& lhs,
    const MidiCcDestination& rhs
) {
    return lhs.routeValidity == rhs.routeValidity &&
           sameIdentity(lhs.identity, rhs.identity);
}

FLASHMEM bool sameRegion(
    const oc::note::sequencer::StepSequencerPlaybackRegion& lhs,
    const oc::note::sequencer::StepSequencerPlaybackRegion& rhs
) {
    return lhs.contentLength == rhs.contentLength &&
           lhs.playStart == rhs.playStart &&
           lhs.loopStart == rhs.loopStart &&
           lhs.loopEnd == rhs.loopEnd;
}

FLASHMEM oc::note::sequencer::StepSequencerPlaybackRegion effectiveRegion(
    const SequencerCcLaneTrackRuntimeInput& input
) {
    if (input.playbackRegion.isValid()) return input.playbackRegion;
    if (input.patternLength == 0U ||
        input.patternLength > core::state::sequencer::SequencerCcLaneBank::MAX_STEPS) {
        return {0, 0, 0, 0};
    }
    return oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(
        input.patternLength
    );
}

FLASHMEM bool validEmissionMode(SequencerCcLaneEmissionMode mode) {
    return mode == SequencerCcLaneEmissionMode::CURRENT_TICK ||
           mode == SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD;
}

FLASHMEM bool hasAuthoredEventInLookaheadWindow(
    const core::state::sequencer::SequencerCcLane& lane,
    const oc::note::sequencer::StepSequencerPlaybackRegion& region,
    uint32_t startOrdinal,
    uint32_t playbackOrdinal
) {
    const uint32_t delta = playbackOrdinal - startOrdinal;
    for (uint32_t distance = 1U; distance <= delta; ++distance) {
        oc::note::sequencer::StepSequencerPlaybackPosition position{};
        if (!oc::note::sequencer::tryResolvePlaybackOrdinal(
                region,
                startOrdinal + distance,
                position
            )) {
            return false;
        }
        if (lane.activeMask.test(position.stepIndex)) return true;
    }
    return false;
}

}  // namespace

FLASHMEM SequencerCcLaneRuntime::SequencerCcLaneRuntime() {
    resetProject();
}

FLASHMEM void SequencerCcLaneRuntime::resetProject() {
    states_ = {};
    track_projection_states_ = {};
    pending_states_ = {};
    pending_track_projection_states_ = {};
    pending_frame_ = {};
}

bool SequencerCcLaneRuntime::seedFrom(
    const SequencerCcLaneRuntime& source
) {
    if (this == &source) return false;
    states_ = source.states_;
    track_projection_states_ = source.track_projection_states_;
    return true;
}

SequencerCcLaneRuntimeStatus SequencerCcLaneRuntime::buildMusicalTickFrame(
    const Inputs& inputs,
    bool playing,
    SequencerCcLaneRuntimeFrame& out
) {
    pending_frame_ = {};
    pending_states_ = states_;
    pending_track_projection_states_ = track_projection_states_;

    if (!playing) {
        out = pending_frame_;
        return SequencerCcLaneRuntimeStatus::OK;
    }

    for (uint8_t track = 0; track < inputs.size(); ++track) {
        const auto& input = inputs[track];
        const uint32_t lookaheadDelta =
            input.playbackOrdinal - input.lookaheadStartOrdinal;
        if (!validEmissionMode(input.emissionMode) ||
            (input.emissionMode ==
                 SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD &&
             lookaheadDelta > MAX_LOOKAHEAD_ORDINAL_DELTA)) {
            pending_frame_ = {};
            pending_frame_.status = SequencerCcLaneRuntimeStatus::INVALID_INPUT;
            out = pending_frame_;
            return pending_frame_.status;
        }
        if (input.frozen) {
            bool retained = false;
            for (uint8_t laneIndex = 0; laneIndex < LANE_COUNT; ++laneIndex) {
                const auto& runtime = pending_states_[stateIndex_(track, laneIndex)];
                // A staged Track may already expose the replacement bank while
                // the audible runtime is intentionally frozen until its musical
                // activation boundary. Publishing the staged generation here
                // would cancel the old Track's delayed CC plan too early.
                pending_frame_.lifecycleGenerations[
                    stateIndex_(track, laneIndex)
                ] = runtime.lifecycleGeneration;
                if (!runtime.occupiedObserved || !runtime.hasHeldValue ||
                    !runtime.hasResolvedDestination) {
                    continue;
                }
                if (pending_frame_.candidateCount >= pending_frame_.candidates.size()) {
                    pending_frame_ = {};
                    pending_frame_.status =
                        SequencerCcLaneRuntimeStatus::CAPACITY_EXCEEDED;
                    out = pending_frame_;
                    return pending_frame_.status;
                }
                const uint8_t outputIndex = pending_frame_.candidateCount++;
                const SequencerCcLaneAddress address{track, laneIndex};
                pending_frame_.candidates[outputIndex] = MidiCcCandidate{
                    .destination = runtime.lastDestination,
                    .author = core::state::shared::MidiCcAuthor{
                        .candidateClass = MidiCcCandidateClass::SEQUENCER_CC_LANE,
                        .stableAddress =
                            core::state::sequencer::sequencerCcLaneStableAddress(
                                track,
                                laneIndex
                            ),
                    },
                    .localValue = runtime.heldValue,
                };
                pending_frame_.contributions[outputIndex] =
                    SequencerCcLaneRuntimeContribution{
                        .address = address,
                        .destination = runtime.lastDestination,
                        .heldValue = runtime.heldValue,
                    };
                if (runtime.lastDestination.routeValidity ==
                    MidiCcRouteValidity::NO_ROUTE) {
                    ++pending_frame_.noRouteCount;
                }
                retained = true;
            }
            if (retained) ++pending_frame_.suppressedTrackCount;
            continue;
        }
        if (input.lanes != nullptr) {
            for (uint8_t lane = 0U; lane < LANE_COUNT; ++lane) {
                pending_frame_.lifecycleGenerations[stateIndex_(track, lane)] =
                    input.lanes->lanes[lane].lifecycleGeneration;
            }
        }
        const auto region = effectiveRegion(input);
        const bool predictive = input.emissionMode ==
            SequencerCcLaneEmissionMode::PREDICTIVE_LOOKAHEAD;
        const uint32_t playbackOrdinal =
            predictive || input.playbackRegion.isValid()
            ? input.playbackOrdinal
            : input.step;
        oc::note::sequencer::StepSequencerPlaybackPosition playback{};
        oc::note::sequencer::StepSequencerPlaybackPosition lookaheadStart{};
        if (!region.isValid() ||
            !oc::note::sequencer::tryResolvePlaybackOrdinal(
                region,
                playbackOrdinal,
                playback
            ) ||
            playback.stepIndex != input.step ||
            (predictive &&
             !oc::note::sequencer::tryResolvePlaybackOrdinal(
                 region,
                 input.lookaheadStartOrdinal,
                 lookaheadStart
             )) ||
            input.ticksPerStep == 0 ||
            input.tickInStep >= input.ticksPerStep) {
            pending_frame_ = {};
            pending_frame_.status = SequencerCcLaneRuntimeStatus::INVALID_INPUT;
            out = pending_frame_;
            return pending_frame_.status;
        }

        auto& projectionState = pending_track_projection_states_[track];
        if (!projectionState.observed || !sameRegion(projectionState.region, region)) {
            for (uint8_t lane = 0; lane < LANE_COUNT; ++lane) {
                pending_states_[stateIndex_(track, lane)] = {};
            }
            projectionState.region = region;
            projectionState.observed = true;
        }
        if (input.lanes == nullptr) {
            // A missing bank is the canonical V3/empty Pattern projection.
            for (uint8_t lane = 0; lane < LANE_COUNT; ++lane) {
                pending_states_[stateIndex_(track, lane)] = {};
            }
            continue;
        }
        if (!core::state::sequencer::validSequencerCcLaneBank(*input.lanes) ||
            input.step >= region.contentLength) {
            pending_frame_ = {};
            pending_frame_.status = SequencerCcLaneRuntimeStatus::INVALID_INPUT;
            out = pending_frame_;
            return pending_frame_.status;
        }

        bool trackSuppressed = false;
        for (uint8_t laneIndex = 0; laneIndex < LANE_COUNT; ++laneIndex) {
            const auto& lane = input.lanes->lanes[laneIndex];
            auto& runtime = pending_states_[stateIndex_(track, laneIndex)];
            if (!lane.occupied) {
                runtime = {};
                continue;
            }

            if (!runtime.occupiedObserved ||
                runtime.lifecycleGeneration != lane.lifecycleGeneration) {
                runtime = {};
                runtime.occupiedObserved = true;
                runtime.lifecycleGeneration = lane.lifecycleGeneration;
            }

            if (!input.enabled || input.muted) {
                trackSuppressed = true;
                continue;
            }

            const bool authoredEvent = predictive
                ? hasAuthoredEventInLookaheadWindow(
                      lane,
                      region,
                      input.lookaheadStartOrdinal,
                      playbackOrdinal
                  )
                : input.stepTriggered && lane.activeMask.test(input.step);
            // A newly observed lane generation (paste, Undo/Redo, restore or
            // region reset) must not resurrect an event that occurred before
            // the observation boundary. The pure projector can look behind
            // the current ordinal for UI/resync geometry, while the realtime
            // hold becomes audible only after this generation authors its
            // first event. Once held, the same projector provides smooth
            // interpolation and loop-wrap continuity.
            if (!runtime.hasHeldValue && !authoredEvent) {
                continue;
            }

            core::state::sequencer::SequencerCcLaneProjectionSpan span{};
            uint8_t projectedValue = 0;
            const float fraction = static_cast<float>(input.tickInStep) /
                static_cast<float>(input.ticksPerStep);
            const bool hasProjection =
                core::state::sequencer::projectSequencerCcLaneValue(
                    lane,
                    region,
                    playbackOrdinal,
                    fraction,
                    projectedValue,
                    &span
                );
            if (!hasProjection) {
                runtime.hasHeldValue = false;
                runtime.hasResolvedDestination = false;
                continue;
            }

            const uint8_t previousValue = runtime.heldValue;
            const bool hadPreviousValue = runtime.hasHeldValue;
            runtime.heldValue = projectedValue;
            runtime.sourceStep = span.sourceStep;
            runtime.hasHeldValue = true;
            const bool valueChanged = hadPreviousValue &&
                runtime.heldValue != previousValue;

            const auto resolved =
                core::state::sequencer::resolveSequencerCcLaneDestination(
                    lane,
                    input.route
                );
            if (!resolved.ok()) {
                pending_frame_ = {};
                pending_frame_.status =
                    SequencerCcLaneRuntimeStatus::INVALID_INPUT;
                out = pending_frame_;
                return pending_frame_.status;
            }

            const bool routeRetargeted = runtime.hasResolvedDestination &&
                !sameDestination(runtime.lastDestination, resolved.destination);
            runtime.lastDestination = resolved.destination;
            runtime.hasResolvedDestination = true;

            if (pending_frame_.candidateCount >=
                pending_frame_.candidates.size()) {
                pending_frame_ = {};
                pending_frame_.status =
                    SequencerCcLaneRuntimeStatus::CAPACITY_EXCEEDED;
                out = pending_frame_;
                return pending_frame_.status;
            }

            const uint8_t outputIndex = pending_frame_.candidateCount++;
            const SequencerCcLaneAddress address{track, laneIndex};
            pending_frame_.candidates[outputIndex] = MidiCcCandidate{
                .destination = resolved.destination,
                .author = core::state::shared::MidiCcAuthor{
                    .candidateClass = MidiCcCandidateClass::SEQUENCER_CC_LANE,
                    .stableAddress =
                        core::state::sequencer::sequencerCcLaneStableAddress(
                            track,
                            laneIndex
                        ),
                },
                .localValue = runtime.heldValue,
            };
            pending_frame_.contributions[outputIndex] =
                SequencerCcLaneRuntimeContribution{
                    .address = address,
                    .destination = resolved.destination,
                    .heldValue = runtime.heldValue,
                    .lifecycleGeneration = runtime.lifecycleGeneration,
                    .authoredEventThisTick = authoredEvent,
                    .valueChangedThisTick = valueChanged,
                    .routeRetargetedThisTick = routeRetargeted,
                };
            if (authoredEvent) ++pending_frame_.authoredEventCount;
            if (routeRetargeted) ++pending_frame_.routeRetargetCount;
            if (resolved.destination.routeValidity == MidiCcRouteValidity::NO_ROUTE) {
                ++pending_frame_.noRouteCount;
            }
        }
        if (trackSuppressed) ++pending_frame_.suppressedTrackCount;
    }

    // Publish output and held state only after the complete frame validated.
    states_ = pending_states_;
    track_projection_states_ = pending_track_projection_states_;
    out = pending_frame_;
    return SequencerCcLaneRuntimeStatus::OK;
}

FLASHMEM bool SequencerCcLaneRuntime::hasHeldValue(
    uint8_t track,
    uint8_t lane
) const {
    if (track >= TRACK_COUNT || lane >= LANE_COUNT) return false;
    return states_[stateIndex_(track, lane)].hasHeldValue;
}

FLASHMEM uint8_t SequencerCcLaneRuntime::heldValue(
    uint8_t track,
    uint8_t lane
) const {
    if (!hasHeldValue(track, lane)) return 0;
    return states_[stateIndex_(track, lane)].heldValue;
}

}  // namespace core::sequencer

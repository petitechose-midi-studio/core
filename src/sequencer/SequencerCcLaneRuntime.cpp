#include "sequencer/SequencerCcLaneRuntime.hpp"

#include <config/PlatformCompat.hpp>

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

}  // namespace

FLASHMEM SequencerCcLaneRuntime::SequencerCcLaneRuntime() {
    resetProject();
}

FLASHMEM void SequencerCcLaneRuntime::resetProject() {
    states_ = {};
    pending_states_ = {};
    pending_frame_ = {};
}

FLASHMEM void SequencerCcLaneRuntime::resetTrack(uint8_t track) {
    if (track >= TRACK_COUNT) return;
    for (uint8_t lane = 0; lane < LANE_COUNT; ++lane) {
        states_[stateIndex_(track, lane)] = {};
    }
}

SequencerCcLaneRuntimeStatus SequencerCcLaneRuntime::buildMusicalTickFrame(
    const Inputs& inputs,
    bool playing,
    SequencerCcLaneRuntimeFrame& out
) {
    pending_frame_ = {};
    pending_states_ = states_;

    if (!playing) {
        out = pending_frame_;
        return SequencerCcLaneRuntimeStatus::OK;
    }

    for (uint8_t track = 0; track < inputs.size(); ++track) {
        const auto& input = inputs[track];
        if (input.frozen) {
            bool retained = false;
            for (uint8_t laneIndex = 0; laneIndex < LANE_COUNT; ++laneIndex) {
                const auto& runtime = pending_states_[stateIndex_(track, laneIndex)];
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
        if (input.lanes == nullptr) {
            // A missing bank is the canonical V3/empty Pattern projection.
            for (uint8_t lane = 0; lane < LANE_COUNT; ++lane) {
                pending_states_[stateIndex_(track, lane)] = {};
            }
            continue;
        }
        if (!core::state::sequencer::validSequencerCcLaneBank(*input.lanes) ||
            input.step >= SequencerCcLaneBank::MAX_STEPS) {
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

            bool authoredEvent = false;
            if (input.stepTriggered && lane.activeMask.test(input.step)) {
                runtime.heldValue = lane.values[input.step];
                runtime.hasHeldValue = true;
                authoredEvent = true;
            }
            if (!runtime.hasHeldValue) continue;

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

            const bool routeMigrated = runtime.hasResolvedDestination &&
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
                    .authoredEventThisTick = authoredEvent,
                    .routeMigratedThisTick = routeMigrated,
                };
            if (authoredEvent) ++pending_frame_.authoredEventCount;
            if (routeMigrated) ++pending_frame_.routeMigrationCount;
            if (resolved.destination.routeValidity == MidiCcRouteValidity::NO_ROUTE) {
                ++pending_frame_.noRouteCount;
            }
        }
        if (trackSuppressed) ++pending_frame_.suppressedTrackCount;
    }

    // Publish output and held state only after the complete frame validated.
    states_ = pending_states_;
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

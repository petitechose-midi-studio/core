#include "state/sequencer/SequencerCcLaneRouting.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

namespace {

using core::state::shared::MidiCcDestination;
using core::state::shared::MidiCcDestinationIdentity;
using core::state::shared::MidiCcRouteValidity;

FLASHMEM bool validTrackRoute(const SequencerCcTrackRoute& route) {
    if (route.validity == MidiCcRouteValidity::VALID) {
        return route.port != MidiCcDestinationIdentity::INVALID_PORT &&
               route.channel <= 15U;
    }
    if (route.validity != MidiCcRouteValidity::NO_ROUTE) return false;
    return route.channel <= 15U ||
           route.channel == MidiCcDestinationIdentity::INVALID_CHANNEL;
}

FLASHMEM bool sameIdentity(
    const MidiCcDestinationIdentity& lhs,
    const MidiCcDestinationIdentity& rhs
) {
    return core::state::shared::sameMidiCcDestinationIdentity(lhs, rhs);
}

FLASHMEM SequencerCcLane laneFromDraft(const SequencerCcLaneDraft& draft) {
    SequencerCcLane lane{};
    lane.occupied = true;
    lane.acceptedMacroConflict = draft.acceptedMacroConflict;
    lane.initialValue = draft.initialValue;
    lane.lifecycleGeneration = 1;
    lane.destination = draft.destination;
    return lane;
}

}  // namespace

FLASHMEM SequencerCcLaneRouteResolveResult resolveSequencerCcLaneDestination(
    const SequencerCcLane& lane,
    const SequencerCcTrackRoute& trackRoute
) {
    SequencerCcLaneRouteResolveResult result{};
    if (!lane.occupied) {
        result.status = SequencerCcLaneRouteResolveStatus::EMPTY_LANE;
        return result;
    }
    if (!validSequencerCcLane(lane) || !validTrackRoute(trackRoute)) {
        result.status = SequencerCcLaneRouteResolveStatus::INVALID_LANE;
        return result;
    }

    auto& identity = result.destination.identity;
    identity.controller = lane.destination.controller;
    if (lane.destination.routePolicy == SequencerCcLaneRoutePolicy::PINNED) {
        identity.port = lane.destination.pinnedPort;
        identity.channel = lane.destination.pinnedChannel;
        result.destination.routeValidity = MidiCcRouteValidity::VALID;
    } else {
        identity.port = trackRoute.port;
        identity.channel = trackRoute.channel;
        result.destination.routeValidity = trackRoute.validity;
    }
    result.status = SequencerCcLaneRouteResolveStatus::OK;
    return result;
}

FLASHMEM bool sequencerCcLaneDestinationsConflict(
    SequencerCcLaneAddress lhsAddress,
    const SequencerCcLane& lhs,
    const SequencerCcTrackRoute& lhsTrackRoute,
    SequencerCcLaneAddress rhsAddress,
    const SequencerCcLane& rhs,
    const SequencerCcTrackRoute& rhsTrackRoute
) {
    if (!lhsAddress.valid() || !rhsAddress.valid() ||
        !lhs.occupied || !rhs.occupied ||
        lhs.destination.controller != rhs.destination.controller) {
        return false;
    }

    const bool lhsInherits = lhs.destination.routePolicy ==
        SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    const bool rhsInherits = rhs.destination.routePolicy ==
        SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    if (lhsAddress.track == rhsAddress.track && lhsInherits && rhsInherits) {
        return true;
    }

    const bool lhsPinned = lhs.destination.routePolicy ==
        SequencerCcLaneRoutePolicy::PINNED;
    const bool rhsPinned = rhs.destination.routePolicy ==
        SequencerCcLaneRoutePolicy::PINNED;
    if (lhsPinned && rhsPinned &&
        lhs.destination.pinnedPort == rhs.destination.pinnedPort &&
        lhs.destination.pinnedChannel == rhs.destination.pinnedChannel) {
        return true;
    }

    const auto lhsResolved = resolveSequencerCcLaneDestination(lhs, lhsTrackRoute);
    const auto rhsResolved = resolveSequencerCcLaneDestination(rhs, rhsTrackRoute);
    return lhsResolved.ok() && rhsResolved.ok() &&
           lhsResolved.destination.routeValidity == MidiCcRouteValidity::VALID &&
           rhsResolved.destination.routeValidity == MidiCcRouteValidity::VALID &&
           sameIdentity(
               lhsResolved.destination.identity,
               rhsResolved.destination.identity
           );
}

FLASHMEM SequencerCcLaneDuplicatePreflight preflightSequencerCcLaneDestination(
    const SequencerCcProjectRoutingView& project,
    SequencerCcLaneAddress candidateAddress,
    const SequencerCcLaneDraft& candidateDraft
) {
    SequencerCcLaneDuplicatePreflight result{};
    if (!candidateAddress.valid() || !validSequencerCcLaneDraft(candidateDraft)) {
        return result;
    }

    const auto candidate = laneFromDraft(candidateDraft);
    const auto resolved = resolveSequencerCcLaneDestination(
        candidate,
        project[candidateAddress.track].trackRoute
    );
    if (resolved.ok()) result.resolvedCandidate = resolved.destination;

    for (uint8_t track = 0; track < project.size(); ++track) {
        const auto* bank = project[track].lanes;
        if (bank == nullptr) continue;
        for (uint8_t lane = 0; lane < bank->lanes.size(); ++lane) {
            const SequencerCcLaneAddress existingAddress{track, lane};
            if (existingAddress.track == candidateAddress.track &&
                existingAddress.lane == candidateAddress.lane) {
                continue;
            }
            const auto& existing = bank->lanes[lane];
            if (sequencerCcLaneDestinationsConflict(
                    existingAddress,
                    existing,
                    project[track].trackRoute,
                    candidateAddress,
                    candidate,
                    project[candidateAddress.track].trackRoute
                )) {
                result.duplicate = true;
                result.existing = existingAddress;
                return result;
            }
        }
    }
    return result;
}

FLASHMEM SequencerCcLaneConflictScan scanSequencerCcLaneConflicts(
    const SequencerCcProjectRoutingView& project
) {
    SequencerCcLaneConflictScan result{};
    for (uint8_t loserTrack = 0; loserTrack < project.size(); ++loserTrack) {
        const auto* loserBank = project[loserTrack].lanes;
        if (loserBank == nullptr) continue;
        for (uint8_t loserLane = 0; loserLane < loserBank->lanes.size(); ++loserLane) {
            const SequencerCcLaneAddress loserAddress{loserTrack, loserLane};
            const auto& loser = loserBank->lanes[loserLane];
            if (!loser.occupied) continue;

            bool foundWinner = false;
            SequencerCcLaneAddress winnerAddress{};
            for (uint8_t winnerTrack = 0;
                 winnerTrack <= loserTrack && !foundWinner;
                 ++winnerTrack) {
                const auto* winnerBank = project[winnerTrack].lanes;
                if (winnerBank == nullptr) continue;
                for (uint8_t winnerLane = 0;
                     winnerLane < winnerBank->lanes.size();
                     ++winnerLane) {
                    const SequencerCcLaneAddress candidateWinner{
                        winnerTrack,
                        winnerLane,
                    };
                    if (sequencerCcLaneStableAddress(
                            candidateWinner.track,
                            candidateWinner.lane
                        ) >= sequencerCcLaneStableAddress(
                            loserAddress.track,
                            loserAddress.lane
                        )) {
                        break;
                    }
                    if (sequencerCcLaneDestinationsConflict(
                            candidateWinner,
                            winnerBank->lanes[winnerLane],
                            project[winnerTrack].trackRoute,
                            loserAddress,
                            loser,
                            project[loserTrack].trackRoute
                        )) {
                        winnerAddress = candidateWinner;
                        foundWinner = true;
                        break;
                    }
                }
            }

            if (!foundWinner) continue;
            if (result.count >= result.conflicts.size()) {
                result.capacityExceeded = true;
                return result;
            }
            const auto resolved = resolveSequencerCcLaneDestination(
                loser,
                project[loserTrack].trackRoute
            );
            result.conflicts[result.count++] = SequencerCcLaneConflict{
                .winner = winnerAddress,
                .loser = loserAddress,
                .destination = resolved.ok()
                    ? resolved.destination
                    : core::state::shared::MidiCcDestination{},
            };
        }
    }
    return result;
}

}  // namespace core::state::sequencer

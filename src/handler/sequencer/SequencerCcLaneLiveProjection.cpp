#include "handler/sequencer/SequencerCcLaneLiveProjection.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "handler/common/MidiCcGlobalFrameCoordinator.hpp"

namespace core::handler {
namespace {

namespace seq = core::state::sequencer;
namespace shared = core::state::shared;

bool matchesLaneAuthor(
    const shared::MidiCcContributionTelemetry& contribution,
    uint16_t stableAddress
) {
    return contribution.author.candidateClass ==
               shared::MidiCcCandidateClass::SEQUENCER_CC_LANE &&
           contribution.author.stableAddress == stableAddress;
}

}  // namespace

FLASHMEM SequencerCcLaneLiveProjection projectSequencerCcLaneLive(
    const MidiCcGlobalFrameCoordinator* coordinator,
    seq::SequencerCcLaneAddress address,
    const seq::SequencerCcLane& lane,
    const seq::SequencerCcTrackRoute& trackRoute
) {
    SequencerCcLaneLiveProjection out{};
    if (coordinator == nullptr || !address.valid() || !lane.occupied) return out;

    const auto route = seq::resolveSequencerCcLaneDestination(lane, trackRoute);
    if (!route.ok()) return out;

    const auto telemetry = coordinator->readTelemetry();
    if (!telemetry) return out;
    const size_t destinationCount = std::min<size_t>(
        telemetry->destinationCount,
        telemetry->destinations.size()
    );
    const uint16_t stableAddress = seq::sequencerCcLaneStableAddress(
        address.track,
        address.lane
    );
    for (size_t index = 0; index < destinationCount; ++index) {
        const auto& resolved = telemetry->destinations[index];
        if (!shared::sameMidiCcDestinationIdentity(
                resolved.destination.identity,
                route.destination.identity
            )) {
            continue;
        }

        bool lanePresent = matchesLaneAuthor(resolved.winner, stableAddress);
        const size_t firstLoser = std::min<size_t>(
            resolved.firstLoser,
            telemetry->losers.size()
        );
        const size_t loserEnd = std::min<size_t>(
            firstLoser + resolved.loserCount,
            std::min<size_t>(telemetry->loserCount, telemetry->losers.size())
        );
        for (size_t loser = firstLoser; !lanePresent && loser < loserEnd; ++loser) {
            lanePresent = matchesLaneAuthor(
                telemetry->losers[loser],
                stableAddress
            );
        }
        if (!lanePresent) return out;

        out.lanePresent = true;
        out.hasOutput = resolved.shouldEmit;
        out.conflict = resolved.conflict;
        out.outputValue = resolved.finalValue;
        out.winnerClass = resolved.winner.author.candidateClass;
        return out;
    }
    return out;
}

}  // namespace core::handler

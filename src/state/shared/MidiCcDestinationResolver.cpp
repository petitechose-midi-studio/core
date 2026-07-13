#include "state/shared/MidiCcDestinationResolver.hpp"

#include <array>
#include <utility>

#include <config/PlatformCompat.hpp>

namespace core::state::shared {

namespace {

FLASHMEM bool validCandidateClass(MidiCcCandidateClass candidateClass) {
    switch (candidateClass) {
        case MidiCcCandidateClass::LIVE_MANUAL:
        case MidiCcCandidateClass::SEQUENCER_CC_LANE:
        case MidiCcCandidateClass::MACRO_COMPUTED:
        case MidiCcCandidateClass::MACRO_STATIC:
            return true;
        default:
            return false;
    }
}

FLASHMEM bool validRoute(MidiCcRouteValidity routeValidity) {
    return routeValidity == MidiCcRouteValidity::VALID ||
           routeValidity == MidiCcRouteValidity::NO_ROUTE;
}

FLASHMEM bool validMode(MidiCcResolutionMode mode) {
    return mode == MidiCcResolutionMode::PREVIEW ||
           mode == MidiCcResolutionMode::LIVE;
}

FLASHMEM bool validCandidate(const MidiCcCandidate& candidate) {
    if (!validCandidateClass(candidate.author.candidateClass) ||
        !validRoute(candidate.destination.routeValidity) ||
        candidate.destination.identity.controller > 127U ||
        candidate.localValue > 127U) {
        return false;
    }

    const auto& identity = candidate.destination.identity;
    if (candidate.destination.routeValidity == MidiCcRouteValidity::VALID) {
        return identity.port != MidiCcDestinationIdentity::INVALID_PORT &&
               identity.channel <= 15U;
    }

    // NO_ROUTE keeps authored identity, including the same 0xFF sentinels used
    // by current Track routing. Other out-of-domain channel values are rejected.
    return identity.channel <= 15U ||
           identity.channel == MidiCcDestinationIdentity::INVALID_CHANNEL;
}

FLASHMEM int compareDestinationIdentity(
    const MidiCcDestinationIdentity& lhs,
    const MidiCcDestinationIdentity& rhs
) {
    if (lhs.port != rhs.port) return lhs.port < rhs.port ? -1 : 1;
    if (lhs.channel != rhs.channel) return lhs.channel < rhs.channel ? -1 : 1;
    if (lhs.controller != rhs.controller) {
        return lhs.controller < rhs.controller ? -1 : 1;
    }
    return 0;
}

FLASHMEM bool candidateLess(const MidiCcCandidate& lhs, const MidiCcCandidate& rhs) {
    const int destinationOrder = compareDestinationIdentity(
        lhs.destination.identity,
        rhs.destination.identity
    );
    if (destinationOrder != 0) return destinationOrder < 0;

    const uint8_t lhsPriority = midiCcCandidatePriority(lhs.author.candidateClass);
    const uint8_t rhsPriority = midiCcCandidatePriority(rhs.author.candidateClass);
    if (lhsPriority != rhsPriority) return lhsPriority < rhsPriority;
    if (lhs.author.stableAddress != rhs.author.stableAddress) {
        return lhs.author.stableAddress < rhs.author.stableAddress;
    }

    // A stable address should identify one author. These final fields make the
    // ordering total even for malformed duplicate submissions, so insertion
    // order can never leak into the visible result.
    if (lhs.destination.routeValidity != rhs.destination.routeValidity) {
        return static_cast<uint8_t>(lhs.destination.routeValidity) <
               static_cast<uint8_t>(rhs.destination.routeValidity);
    }
    return lhs.localValue < rhs.localValue;
}

FLASHMEM void sortCandidateIndices(
    std::array<uint16_t, MidiCcResolutionTelemetry::MAX_CANDIDATES>& indices,
    uint16_t count,
    const MidiCcCandidate* candidates
) {
    // Iterative Shell sort: fixed scratch, no recursion, no allocator, and a
    // deterministic total comparator for the bounded V1 candidate envelope.
    uint16_t gap = 1;
    while (gap < count / 3U) {
        gap = static_cast<uint16_t>(gap * 3U + 1U);
    }

    while (gap > 0) {
        for (uint16_t i = gap; i < count; ++i) {
            const uint16_t candidateIndex = indices[i];
            uint16_t position = i;
            while (position >= gap &&
                   candidateLess(
                       candidates[candidateIndex],
                       candidates[indices[position - gap]]
                   )) {
                indices[position] = indices[position - gap];
                position = static_cast<uint16_t>(position - gap);
            }
            indices[position] = candidateIndex;
        }
        if (gap == 1U) break;
        gap = static_cast<uint16_t>(gap / 3U);
    }
}

FLASHMEM MidiCcContributionTelemetry contributionFor(const MidiCcCandidate& candidate) {
    return MidiCcContributionTelemetry{
        .author = candidate.author,
        .localValue = candidate.localValue,
        .routeValidity = candidate.destination.routeValidity,
    };
}

}  // namespace

FLASHMEM bool sameMidiCcDestinationIdentity(
    const MidiCcDestinationIdentity& lhs,
    const MidiCcDestinationIdentity& rhs
) {
    return compareDestinationIdentity(lhs, rhs) == 0;
}

FLASHMEM uint8_t midiCcCandidatePriority(MidiCcCandidateClass candidateClass) {
    switch (candidateClass) {
        case MidiCcCandidateClass::LIVE_MANUAL:
            return 0;
        case MidiCcCandidateClass::SEQUENCER_CC_LANE:
            return 1;
        case MidiCcCandidateClass::MACRO_COMPUTED:
            return 2;
        case MidiCcCandidateClass::MACRO_STATIC:
            return 3;
        default:
            return 0xFF;
    }
}

FLASHMEM MidiCcResolveStatus resolveMidiCcDestinations(
    const MidiCcCandidate* candidates,
    std::size_t candidateCount,
    MidiCcResolutionMode mode,
    MidiCcResolutionTelemetry& out
) {
    if (candidateCount > MidiCcResolutionTelemetry::MAX_CANDIDATES) {
        return MidiCcResolveStatus::CAPACITY_EXCEEDED;
    }
    if (!validMode(mode) || (candidateCount > 0 && candidates == nullptr)) {
        return MidiCcResolveStatus::INVALID_INPUT;
    }
    for (std::size_t i = 0; i < candidateCount; ++i) {
        if (!validCandidate(candidates[i])) {
            return MidiCcResolveStatus::INVALID_INPUT;
        }
    }

    // All possible failures are resolved before publishing any output.
    out = MidiCcResolutionTelemetry{};
    out.mode = mode;
    out.candidateCount = static_cast<uint16_t>(candidateCount);
    if (candidateCount == 0) return MidiCcResolveStatus::OK;

    std::array<uint16_t, MidiCcResolutionTelemetry::MAX_CANDIDATES> indices{};
    for (uint16_t i = 0; i < out.candidateCount; ++i) {
        indices[i] = i;
    }
    sortCandidateIndices(indices, out.candidateCount, candidates);

    uint16_t cursor = 0;
    while (cursor < out.candidateCount) {
        const MidiCcCandidate& winner = candidates[indices[cursor]];
        auto& resolved = out.destinations[out.destinationCount++];
        resolved.destination = winner.destination;
        resolved.winner = contributionFor(winner);
        resolved.firstLoser = out.loserCount;
        resolved.finalValue = winner.localValue;
        resolved.shouldEmit = mode == MidiCcResolutionMode::LIVE &&
                              winner.destination.routeValidity == MidiCcRouteValidity::VALID;

        ++cursor;
        while (cursor < out.candidateCount &&
               sameMidiCcDestinationIdentity(
                   winner.destination.identity,
                   candidates[indices[cursor]].destination.identity
               )) {
            out.losers[out.loserCount++] = contributionFor(candidates[indices[cursor]]);
            ++resolved.loserCount;
            ++cursor;
        }

        resolved.conflict = resolved.loserCount > 0;
        if (resolved.conflict) ++out.conflictCount;
        if (resolved.shouldEmit) ++out.emissionCount;
        if (winner.destination.routeValidity == MidiCcRouteValidity::NO_ROUTE) {
            ++out.noRouteCount;
        }
    }

    return MidiCcResolveStatus::OK;
}

}  // namespace core::state::shared

#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/sequencer/SequencerCcLaneDomain.hpp"

namespace core::state::sequencer {

struct SequencerCcTrackRoute {
    uint8_t port =
        core::state::shared::MidiCcDestinationIdentity::INVALID_PORT;
    uint8_t channel =
        core::state::shared::MidiCcDestinationIdentity::INVALID_CHANNEL;
    core::state::shared::MidiCcRouteValidity validity =
        core::state::shared::MidiCcRouteValidity::NO_ROUTE;
};

/**
 * Canonical projection from the Track channel field into CC routing.
 *
 * Project/UI state uses 0xFF for an unassigned MIDI channel.  Never label an
 * arbitrary byte as VALID: an invalid inherited route must still publish a
 * canonical NO_ROUTE author so it replaces any previously held valid route.
 * Explicitly pinned lanes remain independently resolvable from this state.
 */
[[nodiscard]] constexpr SequencerCcTrackRoute makeSequencerCcTrackRoute(
    uint8_t port,
    uint8_t channel
) {
    if (port != core::state::shared::MidiCcDestinationIdentity::INVALID_PORT &&
        channel <= 15U) {
        return {
            .port = port,
            .channel = channel,
            .validity = core::state::shared::MidiCcRouteValidity::VALID,
        };
    }
    return {
        .port = core::state::shared::MidiCcDestinationIdentity::INVALID_PORT,
        .channel = core::state::shared::MidiCcDestinationIdentity::INVALID_CHANNEL,
        .validity = core::state::shared::MidiCcRouteValidity::NO_ROUTE,
    };
}

struct SequencerCcLaneAddress {
    static constexpr uint8_t INVALID_INDEX = 0xFF;

    uint8_t track = INVALID_INDEX;
    uint8_t lane = INVALID_INDEX;

    [[nodiscard]] bool valid() const {
        return track < 16U && lane < SequencerCcLaneBank::MAX_LANES;
    }
};

struct SequencerCcPatternRoutingView {
    const SequencerCcLaneBank* lanes = nullptr;
    SequencerCcTrackRoute trackRoute{};
};

using SequencerCcProjectRoutingView =
    std::array<SequencerCcPatternRoutingView, 16>;

enum class SequencerCcLaneRouteResolveStatus : uint8_t {
    OK = 0,
    EMPTY_LANE,
    INVALID_LANE,
    INVALID_ROUTE,
};

struct SequencerCcLaneRouteResolveResult {
    SequencerCcLaneRouteResolveStatus status =
        SequencerCcLaneRouteResolveStatus::INVALID_LANE;
    core::state::shared::MidiCcDestination destination{};

    [[nodiscard]] bool ok() const {
        return status == SequencerCcLaneRouteResolveStatus::OK;
    }
};

/** Resolve without mutating either Track or lane state. */
SequencerCcLaneRouteResolveResult resolveSequencerCcLaneDestination(
    const SequencerCcLane& lane,
    const SequencerCcTrackRoute& trackRoute
);

[[nodiscard]] constexpr uint16_t sequencerCcLaneStableAddress(
    uint8_t track,
    uint8_t lane
) {
    return static_cast<uint16_t>(
        static_cast<uint16_t>(track) * SequencerCcLaneBank::MAX_LANES + lane
    );
}

/**
 * True when two lanes are known to target the same destination.
 *
 * Same-Track Inherit duplicates and identical pins are known even without a
 * current route. Otherwise both resolved routes must be valid.
 */
[[nodiscard]] bool sequencerCcLaneDestinationsConflict(
    SequencerCcLaneAddress lhsAddress,
    const SequencerCcLane& lhs,
    const SequencerCcTrackRoute& lhsTrackRoute,
    SequencerCcLaneAddress rhsAddress,
    const SequencerCcLane& rhs,
    const SequencerCcTrackRoute& rhsTrackRoute
);

struct SequencerCcLaneDuplicatePreflight {
    bool duplicate = false;
    SequencerCcLaneAddress existing{};
    core::state::shared::MidiCcDestination resolvedCandidate{};
};

/** Global 16-Pattern preflight used by direct creation and Lane Settings. */
SequencerCcLaneDuplicatePreflight preflightSequencerCcLaneDestination(
    const SequencerCcProjectRoutingView& project,
    SequencerCcLaneAddress candidateAddress,
    const SequencerCcLaneDraft& candidateDraft
);

struct SequencerCcLaneConflict {
    SequencerCcLaneAddress winner{};
    SequencerCcLaneAddress loser{};
    core::state::shared::MidiCcDestination destination{};
};

struct SequencerCcLaneConflictScan {
    static constexpr uint8_t MAX_CONFLICTS = 63;

    std::array<SequencerCcLaneConflict, MAX_CONFLICTS> conflicts{};
    uint8_t count = 0;
    bool capacityExceeded = false;
};

/**
 * Reports every losing lane exactly once. Existing duplicates retain the
 * lowest stable address as their deterministic winner.
 */
SequencerCcLaneConflictScan scanSequencerCcLaneConflicts(
    const SequencerCcProjectRoutingView& project
);

static_assert(
    sequencerCcLaneStableAddress(15, SequencerCcLaneBank::MAX_LANES - 1U) == 63U
);
static_assert(std::is_trivially_copyable_v<SequencerCcLaneConflictScan>);

}  // namespace core::state::sequencer

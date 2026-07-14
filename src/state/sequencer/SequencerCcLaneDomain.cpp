#include "state/sequencer/SequencerCcLaneDomain.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

namespace {

FLASHMEM bool validMidi7(uint8_t value) {
    return value <= 127U;
}

FLASHMEM bool sameDestination(
    const SequencerCcLaneDestination& lhs,
    const SequencerCcLaneDestination& rhs
) {
    return lhs.controller == rhs.controller &&
           lhs.minimum == rhs.minimum &&
           lhs.maximum == rhs.maximum &&
           lhs.routePolicy == rhs.routePolicy &&
           lhs.pinnedPort == rhs.pinnedPort &&
           lhs.pinnedChannel == rhs.pinnedChannel;
}

FLASHMEM bool canonicalInactiveLane(const SequencerCcLane& lane) {
    SequencerCcLane expected{};
    expected.lifecycleGeneration = lane.lifecycleGeneration;
    return sameSequencerCcLaneMusicalData(lane, expected) &&
           lane.lifecycleGeneration == expected.lifecycleGeneration;
}

FLASHMEM SequencerCcLaneMutationResult result(
    SequencerCcLaneMutationStatus status,
    uint8_t laneIndex,
    uint8_t value = 0
) {
    return {
        .status = status,
        .laneIndex = laneIndex,
        .value = value,
    };
}

}  // namespace

FLASHMEM bool validSequencerCcLaneRoutePolicy(
    SequencerCcLaneRoutePolicy policy
) {
    return policy == SequencerCcLaneRoutePolicy::INHERIT_TRACK ||
           policy == SequencerCcLaneRoutePolicy::PINNED;
}

FLASHMEM bool validSequencerCcLaneConflictPolicy(
    SequencerCcLaneConflictPolicy policy
) {
    return policy == SequencerCcLaneConflictPolicy::FIXED_PRIORITY;
}

FLASHMEM bool validSequencerCcLaneDestination(
    const SequencerCcLaneDestination& destination
) {
    if (!validSequencerCcLaneRoutePolicy(destination.routePolicy) ||
        !validMidi7(destination.controller) ||
        !validMidi7(destination.minimum) ||
        !validMidi7(destination.maximum) ||
        destination.minimum > destination.maximum) {
        return false;
    }

    if (destination.routePolicy == SequencerCcLaneRoutePolicy::PINNED) {
        return destination.pinnedPort !=
                   core::state::shared::MidiCcDestinationIdentity::INVALID_PORT &&
               destination.pinnedChannel <= 15U;
    }

    // Dormant pin fields are preserved for deterministic policy toggling but
    // remain sanitized even while Inherit Track is selected.
    return destination.pinnedPort !=
               core::state::shared::MidiCcDestinationIdentity::INVALID_PORT &&
           destination.pinnedChannel <= 15U;
}

FLASHMEM bool validSequencerCcLaneDraft(const SequencerCcLaneDraft& draft) {
    return validSequencerCcLaneDestination(draft.destination) &&
           draft.initialValue >= draft.destination.minimum &&
           draft.initialValue <= draft.destination.maximum;
}

FLASHMEM bool validSequencerCcLane(const SequencerCcLane& lane) {
    if (!lane.occupied) return canonicalInactiveLane(lane);
    if (!validSequencerCcLaneConflictPolicy(lane.conflictPolicy) ||
        !validSequencerCcLaneDestination(lane.destination) ||
        lane.initialValue < lane.destination.minimum ||
        lane.initialValue > lane.destination.maximum ||
        lane.lifecycleGeneration == 0) {
        return false;
    }

    for (uint8_t step = 0; step < SequencerCcLaneBank::MAX_STEPS; ++step) {
        if (!lane.activeMask.test(step)) continue;
        if (lane.values[step] < lane.destination.minimum ||
            lane.values[step] > lane.destination.maximum) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool validSequencerCcLaneBank(const SequencerCcLaneBank& bank) {
    if (bank.formatVersion != SequencerCcLaneBank::FORMAT_VERSION) return false;
    for (const auto& lane : bank.lanes) {
        if (!validSequencerCcLane(lane)) return false;
    }
    return true;
}

FLASHMEM bool decodeCanonicalSequencerCcLaneBank(
    const SequencerCcLaneBank& persisted,
    SequencerCcLaneBank& out
) {
    if (persisted.formatVersion != SequencerCcLaneBank::FORMAT_VERSION) {
        return false;
    }

    SequencerCcLaneBank pending{};
    pending.revision = persisted.revision;
    for (uint8_t i = 0; i < persisted.lanes.size(); ++i) {
        const auto& lane = persisted.lanes[i];
        if (!lane.occupied) {
            // Reserved bytes and dormant stale values are deliberately not
            // forwarded. A decoded inactive slot has one canonical form.
            pending.lanes[i] = SequencerCcLane{};
            continue;
        }
        if (!validSequencerCcLane(lane)) return false;
        pending.lanes[i] = lane;
    }
    out = pending;
    return true;
}

FLASHMEM uint8_t sequencerCcLaneCount(const SequencerCcLaneBank& bank) {
    uint8_t count = 0;
    for (const auto& lane : bank.lanes) {
        if (lane.occupied) ++count;
    }
    return count;
}

FLASHMEM int8_t firstFreeSequencerCcLane(const SequencerCcLaneBank& bank) {
    for (uint8_t i = 0; i < bank.lanes.size(); ++i) {
        if (!bank.lanes[i].occupied) return static_cast<int8_t>(i);
    }
    return -1;
}

FLASHMEM SequencerCcLaneMutationResult createSequencerCcLane(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    const SequencerCcLaneDraft& draft
) {
    if (laneIndex >= bank.lanes.size()) {
        return result(SequencerCcLaneMutationStatus::INVALID_LANE, laneIndex);
    }
    if (!validSequencerCcLaneDraft(draft)) {
        return result(SequencerCcLaneMutationStatus::INVALID_DESTINATION, laneIndex);
    }
    if (bank.lanes[laneIndex].occupied) {
        return result(SequencerCcLaneMutationStatus::LANE_OCCUPIED, laneIndex);
    }

    const uint16_t generation = nextSequencerCcLaneLifecycleGeneration(
        bank.lanes[laneIndex].lifecycleGeneration
    );
    auto created = SequencerCcLane{};
    created.occupied = true;
    created.acceptedMacroConflict = draft.acceptedMacroConflict;
    created.conflictPolicy = SequencerCcLaneConflictPolicy::FIXED_PRIORITY;
    created.initialValue = draft.initialValue;
    created.lifecycleGeneration = generation;
    created.destination = draft.destination;
    // activeMask and values intentionally stay empty: lane creation is silent.

    bank.lanes[laneIndex] = created;
    bank.formatVersion = SequencerCcLaneBank::FORMAT_VERSION;
    ++bank.revision;
    return result(SequencerCcLaneMutationStatus::OK, laneIndex);
}

FLASHMEM SequencerCcLaneMutationResult updateSequencerCcLaneSettings(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    const SequencerCcLaneDraft& draft
) {
    if (laneIndex >= bank.lanes.size()) {
        return result(SequencerCcLaneMutationStatus::INVALID_LANE, laneIndex);
    }
    if (!validSequencerCcLaneDraft(draft)) {
        return result(SequencerCcLaneMutationStatus::INVALID_DESTINATION, laneIndex);
    }
    auto& lane = bank.lanes[laneIndex];
    if (!lane.occupied) {
        return result(SequencerCcLaneMutationStatus::LANE_EMPTY, laneIndex);
    }

    // A narrowed range cannot silently clamp authored musical data.
    for (uint8_t step = 0; step < SequencerCcLaneBank::MAX_STEPS; ++step) {
        if (!lane.activeMask.test(step)) continue;
        if (lane.values[step] < draft.destination.minimum ||
            lane.values[step] > draft.destination.maximum) {
            return result(
                SequencerCcLaneMutationStatus::INVALID_DESTINATION,
                laneIndex
            );
        }
    }

    if (sameDestination(lane.destination, draft.destination) &&
        lane.initialValue == draft.initialValue &&
        lane.acceptedMacroConflict == draft.acceptedMacroConflict) {
        return result(SequencerCcLaneMutationStatus::NO_CHANGE, laneIndex);
    }

    lane.destination = draft.destination;
    lane.initialValue = draft.initialValue;
    lane.acceptedMacroConflict = draft.acceptedMacroConflict;
    ++bank.revision;
    return result(SequencerCcLaneMutationStatus::OK, laneIndex);
}

FLASHMEM SequencerCcLaneMutationResult setSequencerCcLaneEvent(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    uint8_t step,
    uint8_t value
) {
    if (laneIndex >= bank.lanes.size()) {
        return result(SequencerCcLaneMutationStatus::INVALID_LANE, laneIndex);
    }
    if (step >= SequencerCcLaneBank::MAX_STEPS) {
        return result(SequencerCcLaneMutationStatus::INVALID_STEP, laneIndex);
    }
    auto& lane = bank.lanes[laneIndex];
    if (!lane.occupied) {
        return result(SequencerCcLaneMutationStatus::LANE_EMPTY, laneIndex);
    }
    if (value < lane.destination.minimum || value > lane.destination.maximum) {
        return result(
            SequencerCcLaneMutationStatus::INVALID_DESTINATION,
            laneIndex,
            value
        );
    }
    if (lane.activeMask.test(step) && lane.values[step] == value) {
        return result(SequencerCcLaneMutationStatus::NO_CHANGE, laneIndex, value);
    }

    lane.values[step] = value;
    lane.activeMask.setBit(step);
    ++bank.revision;
    return result(SequencerCcLaneMutationStatus::OK, laneIndex, value);
}

FLASHMEM SequencerCcLaneMutationResult clearSequencerCcLaneEvent(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    uint8_t step
) {
    if (laneIndex >= bank.lanes.size()) {
        return result(SequencerCcLaneMutationStatus::INVALID_LANE, laneIndex);
    }
    if (step >= SequencerCcLaneBank::MAX_STEPS) {
        return result(SequencerCcLaneMutationStatus::INVALID_STEP, laneIndex);
    }
    auto& lane = bank.lanes[laneIndex];
    if (!lane.occupied) {
        return result(SequencerCcLaneMutationStatus::LANE_EMPTY, laneIndex);
    }
    if (!lane.activeMask.test(step)) {
        return result(SequencerCcLaneMutationStatus::NO_CHANGE, laneIndex);
    }

    lane.activeMask.setBit(step, false);
    lane.values[step] = 0;
    ++bank.revision;
    return result(SequencerCcLaneMutationStatus::OK, laneIndex);
}

FLASHMEM SequencerCcLaneMutationResult removeSequencerCcLane(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex
) {
    if (laneIndex >= bank.lanes.size()) {
        return result(SequencerCcLaneMutationStatus::INVALID_LANE, laneIndex);
    }
    auto& lane = bank.lanes[laneIndex];
    if (!lane.occupied) {
        return result(SequencerCcLaneMutationStatus::LANE_EMPTY, laneIndex);
    }

    const uint16_t generation =
        nextSequencerCcLaneLifecycleGeneration(lane.lifecycleGeneration);
    lane = SequencerCcLane{};
    lane.lifecycleGeneration = generation;
    ++bank.revision;
    return result(SequencerCcLaneMutationStatus::OK, laneIndex);
}

FLASHMEM uint8_t proposedSequencerCcLaneEventValue(
    const SequencerCcLane& lane,
    uint8_t step,
    uint8_t patternLength
) {
    if (!lane.occupied) return 0;
    const uint8_t length = std::min<uint8_t>(
        patternLength,
        SequencerCcLaneBank::MAX_STEPS
    );
    if (length > 0 && step < length && lane.activeMask.test(step)) {
        return lane.values[step];
    }
    return lane.initialValue;
}

FLASHMEM bool sameSequencerCcLaneMusicalData(
    const SequencerCcLane& lhs,
    const SequencerCcLane& rhs
) {
    return lhs.occupied == rhs.occupied &&
           lhs.acceptedMacroConflict == rhs.acceptedMacroConflict &&
           lhs.conflictPolicy == rhs.conflictPolicy &&
           lhs.initialValue == rhs.initialValue &&
           sameDestination(lhs.destination, rhs.destination) &&
           lhs.activeMask == rhs.activeMask &&
           lhs.values == rhs.values;
}

FLASHMEM bool sameSequencerCcLaneBankMusicalData(
    const SequencerCcLaneBank& lhs,
    const SequencerCcLaneBank& rhs
) {
    for (uint8_t i = 0; i < lhs.lanes.size(); ++i) {
        if (!sameSequencerCcLaneMusicalData(lhs.lanes[i], rhs.lanes[i])) {
            return false;
        }
    }
    return true;
}

}  // namespace core::state::sequencer

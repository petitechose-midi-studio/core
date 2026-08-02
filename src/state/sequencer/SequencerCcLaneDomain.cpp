#include "state/sequencer/SequencerCcLaneDomain.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

namespace {

FLASHMEM bool validMidi7(uint8_t value) {
    return value <= 127U;
}

FLASHMEM bool validTransition(SequencerCcLaneTransition transition) {
    return static_cast<uint8_t>(transition) <=
        static_cast<uint8_t>(SequencerCcLaneTransition::EASE_IN_OUT);
}

constexpr uint8_t TRANSITION_BITS = 3U;
constexpr uint8_t TRANSITION_MASK = 0x07U;

FLASHMEM uint8_t packedTransitionValue(
    const SequencerCcLane& lane,
    uint8_t step
) {
    const uint16_t bit = static_cast<uint16_t>(step) * TRANSITION_BITS;
    const uint8_t byte = static_cast<uint8_t>(bit / 8U);
    const uint8_t shift = static_cast<uint8_t>(bit % 8U);
    uint16_t packed = lane.transitions[byte];
    if (shift > 5U && byte + 1U < lane.transitions.size()) {
        packed = static_cast<uint16_t>(
            packed | static_cast<uint16_t>(lane.transitions[byte + 1U] << 8U)
        );
    }
    return static_cast<uint8_t>((packed >> shift) & TRANSITION_MASK);
}

FLASHMEM void setPackedTransitionValue(
    SequencerCcLane& lane,
    uint8_t step,
    uint8_t value
) {
    const uint16_t bit = static_cast<uint16_t>(step) * TRANSITION_BITS;
    const uint8_t byte = static_cast<uint8_t>(bit / 8U);
    const uint8_t shift = static_cast<uint8_t>(bit % 8U);
    uint16_t packed = lane.transitions[byte];
    if (shift > 5U && byte + 1U < lane.transitions.size()) {
        packed = static_cast<uint16_t>(
            packed | static_cast<uint16_t>(lane.transitions[byte + 1U] << 8U)
        );
    }
    const uint16_t mask = static_cast<uint16_t>(TRANSITION_MASK << shift);
    packed = static_cast<uint16_t>(
        (packed & static_cast<uint16_t>(~mask)) |
        (static_cast<uint16_t>(value & TRANSITION_MASK) << shift)
    );
    lane.transitions[byte] = static_cast<uint8_t>(packed & 0xFFU);
    if (shift > 5U && byte + 1U < lane.transitions.size()) {
        lane.transitions[byte + 1U] = static_cast<uint8_t>(packed >> 8U);
    }
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

FLASHMEM bool assignLaneStep(
    SequencerCcLane& lane,
    uint8_t step,
    bool active,
    uint8_t value,
    SequencerCcLaneTransition transition
) {
    const bool currentActive = lane.activeMask.test(step);
    const uint8_t canonicalValue = active ? value : 0U;
    const auto canonicalTransition = active
        ? transition
        : SequencerCcLaneTransition::HOLD;
    if (currentActive == active &&
        lane.values[step] == canonicalValue &&
        sequencerCcLaneTransition(lane, step) == canonicalTransition) {
        return false;
    }

    lane.activeMask.setBit(step, active);
    lane.values[step] = canonicalValue;
    setPackedTransitionValue(
        lane,
        step,
        static_cast<uint8_t>(canonicalTransition)
    );
    return true;
}

FLASHMEM bool assignLaneStepFrom(
    SequencerCcLane& target,
    uint8_t targetStep,
    const SequencerCcLane& source,
    uint8_t sourceStep
) {
    return assignLaneStep(
        target,
        targetStep,
        source.activeMask.test(sourceStep),
        source.values[sourceStep],
        sequencerCcLaneTransition(source, sourceStep)
    );
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

    for (uint16_t step = 0; step < SequencerCcLaneBank::MAX_STEPS; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        if (!validTransition(static_cast<SequencerCcLaneTransition>(
                packedTransitionValue(lane, stepIndex)
            ))) {
            return false;
        }
        if (!lane.activeMask.test(stepIndex)) continue;
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
    if (!validSequencerCcLaneBank(persisted)) return false;
    out = persisted;
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
    for (uint16_t step = 0; step < SequencerCcLaneBank::MAX_STEPS; ++step) {
        const auto stepIndex = static_cast<uint8_t>(step);
        if (!lane.activeMask.test(stepIndex)) continue;
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
    setPackedTransitionValue(lane, step, 0U);
    ++bank.revision;
    return result(SequencerCcLaneMutationStatus::OK, laneIndex);
}

FLASHMEM SequencerCcLaneTransition sequencerCcLaneTransition(
    const SequencerCcLane& lane,
    uint8_t step
) {
    if (step >= SequencerCcLaneBank::MAX_STEPS) {
        return SequencerCcLaneTransition::HOLD;
    }
    return static_cast<SequencerCcLaneTransition>(
        packedTransitionValue(lane, step)
    );
}

FLASHMEM float sequencerCcLaneShapeProgress(
    SequencerCcLaneTransition transition,
    float progress
) {
    if (!std::isfinite(progress)) progress = 0.0f;
    progress = std::clamp(progress, 0.0f, 1.0f);
    switch (transition) {
        case SequencerCcLaneTransition::LINEAR:
            return progress;
        case SequencerCcLaneTransition::EASE_IN:
            return progress * progress;
        case SequencerCcLaneTransition::EASE_OUT: {
            const float inverse = 1.0f - progress;
            return 1.0f - inverse * inverse;
        }
        case SequencerCcLaneTransition::EASE_IN_OUT:
            // Smoothstep: zero slope at both authored endpoints.
            return progress * progress * (3.0f - 2.0f * progress);
        case SequencerCcLaneTransition::HOLD:
        default:
            return 0.0f;
    }
}

FLASHMEM uint8_t interpolateSequencerCcLaneValue(
    uint8_t sourceValue,
    uint8_t targetValue,
    SequencerCcLaneTransition transition,
    float progress
) {
    const float shaped = sequencerCcLaneShapeProgress(transition, progress);
    const float value = static_cast<float>(sourceValue) +
        (static_cast<float>(targetValue) - sourceValue) * shaped;
    return static_cast<uint8_t>(std::clamp<int>(
        static_cast<int>(std::lround(value)),
        0,
        127
    ));
}

FLASHMEM SequencerCcLaneMutationResult setSequencerCcLaneTransition(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    uint8_t step,
    SequencerCcLaneTransition transition
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
    if (!lane.activeMask.test(step) || !validTransition(transition)) {
        return result(SequencerCcLaneMutationStatus::INVALID_DESTINATION, laneIndex);
    }
    if (sequencerCcLaneTransition(lane, step) == transition) {
        return result(SequencerCcLaneMutationStatus::NO_CHANGE, laneIndex);
    }
    setPackedTransitionValue(
        lane,
        step,
        static_cast<uint8_t>(transition)
    );
    ++bank.revision;
    return result(
        SequencerCcLaneMutationStatus::OK,
        laneIndex,
        static_cast<uint8_t>(transition)
    );
}

FLASHMEM SequencerCcLaneMutationResult deleteSequencerCcLane(
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

FLASHMEM bool trimSequencerCcLaneBank(
    SequencerCcLaneBank& bank,
    uint8_t contentLength
) {
    if (contentLength == 0U || contentLength > SequencerCcLaneBank::MAX_STEPS) {
        return false;
    }

    bool changed = false;
    for (auto& lane : bank.lanes) {
        if (!lane.occupied) continue;
        for (uint16_t step = contentLength; step < SequencerCcLaneBank::MAX_STEPS; ++step) {
            changed = assignLaneStep(
                lane,
                static_cast<uint8_t>(step),
                false,
                0,
                SequencerCcLaneTransition::HOLD
            ) || changed;
        }
    }
    if (changed) ++bank.revision;
    return changed;
}

FLASHMEM bool duplicateSequencerCcLaneBankRange(
    SequencerCcLaneBank& bank,
    uint8_t sourceStart,
    uint8_t targetStart,
    uint8_t count
) {
    if (count == 0U ||
        static_cast<uint16_t>(sourceStart) + count > SequencerCcLaneBank::MAX_STEPS ||
        static_cast<uint16_t>(targetStart) + count > SequencerCcLaneBank::MAX_STEPS) {
        return false;
    }

    bool changed = false;
    for (auto& lane : bank.lanes) {
        if (!lane.occupied) continue;
        const SequencerCcLane source = lane;
        for (uint16_t offset = 0; offset < count; ++offset) {
            changed = assignLaneStepFrom(
                lane,
                static_cast<uint8_t>(targetStart + offset),
                source,
                static_cast<uint8_t>(sourceStart + offset)
            ) || changed;
        }
    }
    if (changed) ++bank.revision;
    return changed;
}

FLASHMEM bool rotateSequencerCcLaneBank(
    SequencerCcLaneBank& bank,
    uint8_t contentLength,
    int offsetSteps
) {
    if (contentLength <= 1U || contentLength > SequencerCcLaneBank::MAX_STEPS) {
        return false;
    }
    int normalized = offsetSteps % static_cast<int>(contentLength);
    if (normalized < 0) normalized += contentLength;
    if (normalized == 0) return false;

    bool changed = false;
    for (auto& lane : bank.lanes) {
        if (!lane.occupied) continue;
        const SequencerCcLane source = lane;
        for (uint16_t sourceStep = 0; sourceStep < contentLength; ++sourceStep) {
            const auto sourceIndex = static_cast<uint8_t>(sourceStep);
            const uint8_t targetStep = static_cast<uint8_t>(
                (sourceStep + normalized) % contentLength
            );
            changed = assignLaneStepFrom(
                lane, targetStep, source, sourceIndex
            ) || changed;
        }
    }
    if (changed) ++bank.revision;
    return changed;
}

FLASHMEM bool insertSequencerCcLaneBankSpan(
    SequencerCcLaneBank& bank,
    uint8_t oldContentLength,
    uint8_t insertAt,
    uint8_t insertedLength
) {
    const uint16_t newLength = static_cast<uint16_t>(oldContentLength) + insertedLength;
    if (oldContentLength == 0U || insertedLength == 0U ||
        insertAt > oldContentLength || newLength > SequencerCcLaneBank::MAX_STEPS) {
        return false;
    }

    bool changed = false;
    for (auto& lane : bank.lanes) {
        if (!lane.occupied) continue;
        const SequencerCcLane source = lane;
        for (uint16_t step = insertAt; step < newLength; ++step) {
            const uint8_t targetStep = static_cast<uint8_t>(step);
            if (step < static_cast<uint16_t>(insertAt) + insertedLength) {
                changed = assignLaneStep(
                    lane,
                    targetStep,
                    false,
                    0,
                    SequencerCcLaneTransition::HOLD
                ) || changed;
                continue;
            }
            changed = assignLaneStepFrom(
                lane,
                targetStep,
                source,
                static_cast<uint8_t>(step - insertedLength)
            ) || changed;
        }
    }
    if (changed) ++bank.revision;
    return changed;
}

FLASHMEM bool removeSequencerCcLaneBankSpan(
    SequencerCcLaneBank& bank,
    uint8_t oldContentLength,
    uint8_t removeAt,
    uint8_t removedLength
) {
    const uint16_t removeEnd = static_cast<uint16_t>(removeAt) + removedLength;
    if (oldContentLength <= 1U || removedLength == 0U ||
        removeAt >= oldContentLength || removeEnd > oldContentLength ||
        removedLength >= oldContentLength) {
        return false;
    }

    const uint8_t newLength = static_cast<uint8_t>(oldContentLength - removedLength);
    bool changed = false;
    for (auto& lane : bank.lanes) {
        if (!lane.occupied) continue;
        const SequencerCcLane source = lane;
        for (uint16_t step = removeAt; step < newLength; ++step) {
            const auto stepIndex = static_cast<uint8_t>(step);
            changed = assignLaneStepFrom(
                lane,
                stepIndex,
                source,
                static_cast<uint8_t>(step + removedLength)
            ) || changed;
        }
        for (uint16_t step = newLength; step < oldContentLength; ++step) {
            changed = assignLaneStep(
                lane,
                static_cast<uint8_t>(step),
                false,
                0,
                SequencerCcLaneTransition::HOLD
            ) || changed;
        }
    }
    if (changed) ++bank.revision;
    return changed;
}

FLASHMEM SequencerCcLaneBatchMutationResult
removeSequencerCcLaneBankStepsUnversioned(
    SequencerCcLaneBank& bank,
    uint8_t oldContentLength,
    const oc::note::sequencer::StepBitMask128& removalMask
) noexcept {
    if (oldContentLength == 0U ||
        oldContentLength > SequencerCcLaneBank::MAX_STEPS ||
        (removalMask &
         ~oc::note::sequencer::StepBitMask128::prefixMask(oldContentLength)) !=
            oc::note::sequencer::StepBitMask128{}) {
        return {.status = SequencerCcLaneBatchMutationStatus::INVALID_ARGUMENT};
    }

    uint8_t removedCount = 0U;
    for (uint16_t step = 0; step < oldContentLength; ++step) {
        if (removalMask.test(static_cast<uint8_t>(step))) {
            ++removedCount;
        }
    }
    if (removedCount == 0U || removedCount >= oldContentLength) {
        return {.status = SequencerCcLaneBatchMutationStatus::INVALID_ARGUMENT};
    }
    if (!validSequencerCcLaneBank(bank)) {
        return {.status = SequencerCcLaneBatchMutationStatus::INVALID_BANK};
    }

    const uint8_t newContentLength =
        static_cast<uint8_t>(oldContentLength - removedCount);
    bool changed = false;
    for (auto& lane : bank.lanes) {
        if (!lane.occupied) continue;

        uint8_t destination = 0U;
        for (uint16_t source = 0; source < oldContentLength; ++source) {
            const auto sourceStep = static_cast<uint8_t>(source);
            if (removalMask.test(sourceStep)) continue;
            if (destination != sourceStep) {
                // destination is always lower than source. Reading and writing
                // the same lane is therefore stable without a lane-sized copy.
                changed = assignLaneStepFrom(
                    lane,
                    destination,
                    lane,
                    sourceStep
                ) || changed;
            }
            ++destination;
        }

        // Match the established span-removal contract: only the former active
        // range is structurally shifted/cleared. Authored cold events beyond
        // oldContentLength remain byte-for-byte available for a later extend.
        for (uint16_t step = newContentLength;
             step < oldContentLength;
             ++step) {
            changed = assignLaneStep(
                lane,
                static_cast<uint8_t>(step),
                false,
                0,
                SequencerCcLaneTransition::HOLD
            ) || changed;
        }
    }

    return {
        .status = changed
            ? SequencerCcLaneBatchMutationStatus::APPLIED
            : SequencerCcLaneBatchMutationStatus::NO_CHANGE,
    };
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
           lhs.values == rhs.values &&
           lhs.transitions == rhs.transitions;
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

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

#include <oc/note/sequencer/StepBitMask128.hpp>

#include "state/shared/MidiCcDestinationResolver.hpp"

namespace core::state::sequencer {

/** Route ownership for one Pattern-local classic MIDI CC lane. */
enum class SequencerCcLaneRoutePolicy : uint8_t {
    INHERIT_TRACK = 0,
    PINNED,
};

/** Persisted V1 conflict policy. Classic MIDI CC is never summed implicitly. */
enum class SequencerCcLaneConflictPolicy : uint8_t {
    FIXED_PRIORITY = 0,
};

/** Shape owned by an authored event until the next authored event. */
enum class SequencerCcLaneTransition : uint8_t {
    HOLD = 0,
    LINEAR,
    EASE_IN,
    EASE_OUT,
    EASE_IN_OUT,
};

/**
 * Persisted destination and edit range for one lane.
 *
 * Port remains explicit even while the product exposes only one output. The
 * pinned identity never borrows fields from Track routing.
 */
struct SequencerCcLaneDestination {
    uint8_t controller = 74;
    uint8_t minimum = 0;
    uint8_t maximum = 127;
    SequencerCcLaneRoutePolicy routePolicy =
        SequencerCcLaneRoutePolicy::INHERIT_TRACK;
    uint8_t pinnedPort = 0;
    uint8_t pinnedChannel = 0;
};

/**
 * Bounded sparse lane persisted inside one Pattern.
 *
 * values are meaningful only where activeMask is set. initialValue is an edit
 * proposal for the first authored event; creating the lane itself is silent.
 */
struct SequencerCcLane {
    bool occupied = false;
    bool acceptedMacroConflict = false;
    SequencerCcLaneConflictPolicy conflictPolicy =
        SequencerCcLaneConflictPolicy::FIXED_PRIORITY;
    uint8_t initialValue = 64;
    uint16_t lifecycleGeneration = 0;
    SequencerCcLaneDestination destination{};
    oc::note::sequencer::StepBitMask128 activeMask{};
    std::array<uint8_t, 128> values{};
    // Three bits per step. This keeps all five semantic shapes bounded while
    // leaving the lane payload compact in PSRAM (48 bytes for 128 steps).
    std::array<uint8_t, 48> transitions{};
};

/**
 * Four-lane V1 Pattern payload. No operation allocates.
 *
 * Firmware PatternState must own this payload through ExtmemUniquePtr and
 * materialize it lazily. Embedding 17 copies in CoreState would consume RAM1.
 */
struct SequencerCcLaneBank {
    static constexpr uint8_t FORMAT_VERSION = 3;
    static constexpr uint8_t MAX_LANES = 4;
    static constexpr uint8_t MAX_STEPS = 128;

    uint8_t formatVersion = FORMAT_VERSION;
    uint32_t revision = 0;
    std::array<SequencerCcLane, MAX_LANES> lanes{};
};

/** Non-persisted draft used only by transactional Lane Settings. */
struct SequencerCcLaneDraft {
    SequencerCcLaneDestination destination{};
    uint8_t initialValue = 64;
    bool acceptedMacroConflict = false;
};

enum class SequencerCcLaneMutationStatus : uint8_t {
    OK = 0,
    NO_CHANGE,
    INVALID_LANE,
    INVALID_STEP,
    INVALID_DESTINATION,
    LANE_OCCUPIED,
    LANE_EMPTY,
    CAPACITY_EXCEEDED,
};

struct SequencerCcLaneMutationResult {
    SequencerCcLaneMutationStatus status =
        SequencerCcLaneMutationStatus::INVALID_DESTINATION;
    uint8_t laneIndex = SequencerCcLaneBank::MAX_LANES;
    uint8_t value = 0;

    [[nodiscard]] bool changed() const {
        return status == SequencerCcLaneMutationStatus::OK;
    }
};

/** Monotonic non-zero identity used to invalidate held runtime state. */
[[nodiscard]] constexpr uint16_t nextSequencerCcLaneLifecycleGeneration(
    uint16_t current
) {
    const uint16_t next = static_cast<uint16_t>(current + 1U);
    return next == 0 ? 1U : next;
}

[[nodiscard]] bool validSequencerCcLaneRoutePolicy(
    SequencerCcLaneRoutePolicy policy
);
[[nodiscard]] bool validSequencerCcLaneConflictPolicy(
    SequencerCcLaneConflictPolicy policy
);
[[nodiscard]] bool validSequencerCcLaneDestination(
    const SequencerCcLaneDestination& destination
);
[[nodiscard]] bool validSequencerCcLaneDraft(const SequencerCcLaneDraft& draft);
[[nodiscard]] bool validSequencerCcLane(const SequencerCcLane& lane);
[[nodiscard]] bool validSequencerCcLaneBank(const SequencerCcLaneBank& bank);

/**
 * Strict decode boundary. Inactive lanes are canonicalized; occupied malformed
 * or future-version data is rejected without partially publishing `out`.
 */
[[nodiscard]] bool decodeCanonicalSequencerCcLaneBank(
    const SequencerCcLaneBank& persisted,
    SequencerCcLaneBank& out
);

[[nodiscard]] uint8_t sequencerCcLaneCount(const SequencerCcLaneBank& bank);
[[nodiscard]] int8_t firstFreeSequencerCcLane(const SequencerCcLaneBank& bank);

/** Create one empty/silent lane. No active event is synthesized. */
SequencerCcLaneMutationResult createSequencerCcLane(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    const SequencerCcLaneDraft& draft
);

SequencerCcLaneMutationResult updateSequencerCcLaneSettings(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    const SequencerCcLaneDraft& draft
);

/**
 * Author or replace one event. Values outside the lane range are rejected;
 * malformed input never partially mutates the bank.
 */
SequencerCcLaneMutationResult setSequencerCcLaneEvent(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    uint8_t step,
    uint8_t value
);

SequencerCcLaneMutationResult clearSequencerCcLaneEvent(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    uint8_t step
);

[[nodiscard]] SequencerCcLaneTransition sequencerCcLaneTransition(
    const SequencerCcLane& lane,
    uint8_t step
);

/** Canonical outgoing-transition math shared by runtime and presentation. */
[[nodiscard]] float sequencerCcLaneShapeProgress(
    SequencerCcLaneTransition transition,
    float progress
);

[[nodiscard]] uint8_t interpolateSequencerCcLaneValue(
    uint8_t sourceValue,
    uint8_t targetValue,
    SequencerCcLaneTransition transition,
    float progress
);

SequencerCcLaneMutationResult setSequencerCcLaneTransition(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex,
    uint8_t step,
    SequencerCcLaneTransition transition
);

SequencerCcLaneMutationResult removeSequencerCcLane(
    SequencerCcLaneBank& bank,
    uint8_t laneIndex
);

/** Bulk Pattern transformations. Each operation bumps bank.revision at most once. */
bool trimSequencerCcLaneBank(SequencerCcLaneBank& bank, uint8_t contentLength);
bool duplicateSequencerCcLaneBankRange(
    SequencerCcLaneBank& bank,
    uint8_t sourceStart,
    uint8_t targetStart,
    uint8_t count
);
bool rotateSequencerCcLaneBank(
    SequencerCcLaneBank& bank,
    uint8_t contentLength,
    int offsetSteps
);
bool insertSequencerCcLaneBankSpan(
    SequencerCcLaneBank& bank,
    uint8_t oldContentLength,
    uint8_t insertAt,
    uint8_t insertedLength
);
bool removeSequencerCcLaneBankSpan(
    SequencerCcLaneBank& bank,
    uint8_t oldContentLength,
    uint8_t removeAt,
    uint8_t removedLength
);

/**
 * Deterministic value proposed when turning an authored `--` cell.
 *
 * `Initial` is the authored edit proposal for every `--` cell. Runtime held
 * value is a separate Live projection and never changes this draft behavior.
 */
[[nodiscard]] uint8_t proposedSequencerCcLaneEventValue(
    const SequencerCcLane& lane,
    uint8_t step,
    uint8_t patternLength
);

[[nodiscard]] bool sameSequencerCcLaneMusicalData(
    const SequencerCcLane& lhs,
    const SequencerCcLane& rhs
);
[[nodiscard]] bool sameSequencerCcLaneBankMusicalData(
    const SequencerCcLaneBank& lhs,
    const SequencerCcLaneBank& rhs
);

static_assert(std::is_standard_layout_v<SequencerCcLane>);
static_assert(std::is_trivially_copyable_v<SequencerCcLane>);
static_assert(std::is_standard_layout_v<SequencerCcLaneBank>);
static_assert(std::is_trivially_copyable_v<SequencerCcLaneBank>);
static_assert(sizeof(SequencerCcLane) <= 208U);
static_assert(sizeof(SequencerCcLaneBank) <= 848U);

}  // namespace core::state::sequencer

#pragma once

#include <cstdint>
#include <type_traits>

#include "state/sequencer/SequencerSnapshots.hpp"

namespace core::state::sequencer {

/**
 * Destructive Pattern randomization targets.
 *
 * This is deliberately independent from StepProperty local-variation
 * capability: State/Density, CC lanes and hierarchical graph content are not
 * Pattern Randomize V1 targets.
 */
enum class SequencerPatternRandomizeProperty : uint8_t {
    NOTE = 0,
    VELOCITY,
    GATE,
    NUDGE,
    PROBABILITY,
};

inline constexpr uint8_t SEQUENCER_PATTERN_RANDOMIZE_AMOUNT_MAX = 100;
inline constexpr uint32_t SEQUENCER_PATTERN_RANDOMIZE_DEFAULT_SEED = 0x6D2B79F5U;

/**
 * Bounded, allocation-free preview draft.
 *
 * Defaults intentionally affect every active Step, with a conservative
 * two-unit Note range. A UI changing `property` should initialize `range`
 * from defaultPatternRandomizeRange(). `range` is an absolute signed-delta
 * bound in native units: semitones/effective-scale degrees, MIDI units, gate
 * percentage points, nudge units, or probability percentage points.
 */
struct SequencerPatternRandomizeDraft {
    SequencerPatternRandomizeProperty property =
        SequencerPatternRandomizeProperty::NOTE;
    uint8_t amount = SEQUENCER_PATTERN_RANDOMIZE_AMOUNT_MAX;
    uint16_t range = 2;
    bool activeOnly = true;
    uint32_t seed = SEQUENCER_PATTERN_RANDOMIZE_DEFAULT_SEED;
};

static_assert(std::is_trivially_copyable_v<SequencerPatternRandomizeDraft>);
static_assert(sizeof(SequencerPatternRandomizeDraft) <= 12U);

struct SequencerPatternRandomizeStepProjection {
    bool eligible = false;
    bool selected = false;
    bool changed = false;
    bool clamped = false;
    int32_t sourceValue = 0;
    int32_t delta = 0;
    int32_t targetValue = 0;
};

struct SequencerPatternRandomizeSummary {
    uint8_t contentLength = 0;
    uint8_t eligibleCount = 0;
    uint8_t selectedCount = 0;
    uint8_t changedCount = 0;
    uint8_t clampedCount = 0;
};

[[nodiscard]] bool isPatternRandomizeProperty(uint8_t value);

[[nodiscard]] SequencerPatternRandomizeProperty sanitizePatternRandomizeProperty(
    uint8_t value
);

[[nodiscard]] uint16_t maxPatternRandomizeRange(
    SequencerPatternRandomizeProperty property
);

[[nodiscard]] uint16_t defaultPatternRandomizeRange(
    SequencerPatternRandomizeProperty property
);

[[nodiscard]] SequencerPatternRandomizeDraft sanitizePatternRandomizeDraft(
    SequencerPatternRandomizeDraft draft
);

/** Pure projection used by both preview and materialization. */
[[nodiscard]] SequencerPatternRandomizeStepProjection projectPatternRandomizeStep(
    const SequencerPatternSnapshot& base,
    const SequencerPatternRandomizeDraft& draft,
    uint8_t step
);

[[nodiscard]] SequencerPatternRandomizeSummary summarizePatternRandomize(
    const SequencerPatternSnapshot& base,
    const SequencerPatternRandomizeDraft& draft
);

/**
 * Copies `base` to `target`, then writes only the selected root Step property.
 * Region/timing/scales/revisions/masks and every non-target array remain exact.
 * Graph and CC ownership are intentionally outside this flat snapshot API.
 */
[[nodiscard]] SequencerPatternRandomizeSummary materializePatternRandomizeSnapshot(
    const SequencerPatternSnapshot& base,
    const SequencerPatternRandomizeDraft& draft,
    SequencerPatternSnapshot& target
);

/** Deterministic next seed; Undo/Redo must persist the materialized target. */
[[nodiscard]] uint32_t rerollPatternRandomizeSeed(uint32_t seed);

}  // namespace core::state::sequencer

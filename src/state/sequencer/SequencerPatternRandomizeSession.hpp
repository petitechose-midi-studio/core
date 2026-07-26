#pragma once

#include <cstdint>

#include "state/sequencer/SequencerPatternRandomizeOps.hpp"

namespace core::state::sequencer {

struct SequencerPatternState;

/** Four controls exposed while the Pattern Randomize draft is auditioned. */
enum class SequencerPatternRandomizeField : uint8_t {
    PROPERTY = 0,
    AMOUNT,
    RANGE,
    SCOPE,
    COUNT,
};

/**
 * Cold, non-published Pattern Randomize session.
 *
 * The feature module owns exactly one instance in PSRAM. `base` never changes
 * during the audition; every edit rematerializes `preview` from that base, so
 * Cancel is free and Apply/Redo never invoke the RNG again.
 */
struct SequencerPatternRandomizeSession {
    bool active = false;
    SequencerPatternRandomizeField focusedField =
        SequencerPatternRandomizeField::PROPERTY;
    uint8_t focusedStep = 0;
    SequencerPatternRandomizeDraft draft{};
    /** Seed reserved for the next session; intentionally survives Cancel/Apply. */
    uint32_t nextSeed = SEQUENCER_PATTERN_RANDOMIZE_DEFAULT_SEED;
    SequencerPatternRandomizeSummary summary{};
    SequencerPatternSnapshot base{};
    SequencerPatternSnapshot preview{};

    void begin(const SequencerPatternSnapshot& source, uint8_t step);
    void begin(const SequencerPatternState& source, uint8_t step);
    void cancel();
    bool moveField(int direction);
    bool setFocusedValue(int32_t value);
    bool reroll();
    void rebuildPreview();

private:
    void beginWithSnapshot_(const SequencerPatternSnapshot& source, uint8_t step);
};

struct SequencerPatternRandomizeValueRange {
    int32_t minimum = 0;
    int32_t maximum = 0;

    [[nodiscard]] constexpr uint32_t count() const {
        return maximum >= minimum
            ? static_cast<uint32_t>(maximum - minimum) + 1U
            : 0U;
    }
};

[[nodiscard]] SequencerPatternRandomizeValueRange
patternRandomizeValueRange(const SequencerPatternRandomizeSession& session);

[[nodiscard]] int32_t patternRandomizeFocusedValue(
    const SequencerPatternRandomizeSession& session
);

static_assert(sizeof(SequencerPatternRandomizeSession) < 2048U);

}  // namespace core::state::sequencer

#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerPlaybackRegion.hpp>

#include "state/sequencer/SequencerClipState.hpp"

namespace core::state::sequencer {

struct SequencerPatternState;
struct SequencerState;

using SequencerClipPlaybackRegion =
    oc::note::sequencer::StepSequencerPlaybackRegion;

[[nodiscard]] uint16_t sequencerTicksPerStep(uint8_t stepsPerBeat) noexcept;
[[nodiscard]] uint16_t patternContentEndTick(
    const SequencerPatternState& pattern
) noexcept;
[[nodiscard]] bool validClipRegion(
    const SequencerPatternState& pattern,
    const SequencerClipState& clip
) noexcept;

/** Projects the canonical tick boundaries onto the current Pattern grid. */
[[nodiscard]] SequencerClipPlaybackRegion clipPlaybackRegion(
    const SequencerPatternState& pattern,
    const SequencerClipState& clip
) noexcept;

/** Builds the canonical tick representation of one valid step region. */
[[nodiscard]] SequencerClipState clipStateForPlaybackRegion(
    const SequencerPatternState& pattern,
    const SequencerClipPlaybackRegion& region
) noexcept;

void resetClipToPattern(
    SequencerClipState& clip,
    const SequencerPatternState& pattern
) noexcept;

SequencerClipPlaybackRegion resizedClipPlaybackRegion(
    const SequencerClipPlaybackRegion& region,
    uint8_t newContentLength
);
SequencerClipPlaybackRegion insertedClipPlaybackRegion(
    const SequencerClipPlaybackRegion& region,
    uint8_t insertAt,
    uint8_t insertedLength
);
SequencerClipPlaybackRegion removedClipPlaybackRegion(
    const SequencerClipPlaybackRegion& region,
    uint8_t removeAt,
    uint8_t removedLength
);

/** Allocation-free pair mutation without active-editor revision publication. */
bool setClipPlaybackRegion(
    SequencerPatternState& pattern,
    SequencerClipState& clip,
    const SequencerClipPlaybackRegion& region
);
bool resizeClipPatternContent(
    SequencerPatternState& pattern,
    SequencerClipState& clip,
    uint8_t newContentLength
);
bool insertClipPatternSpan(
    SequencerPatternState& pattern,
    SequencerClipState& clip,
    uint8_t insertAt,
    uint8_t insertedLength
);
bool removeClipPatternSpan(
    SequencerPatternState& pattern,
    SequencerClipState& clip,
    uint8_t removeAt,
    uint8_t removedLength
);
bool setClipPatternStepsPerBeat(
    SequencerPatternState& pattern,
    SequencerClipState& clip,
    uint8_t stepsPerBeat
);

/** Active-editor wrappers publish the Clip revision exactly once. */
bool setClipPlaybackRegion(
    SequencerState& sequencer,
    const SequencerClipPlaybackRegion& region
);
bool resizeClipPatternContent(
    SequencerState& sequencer,
    uint8_t newContentLength
);
bool insertClipPatternSpan(
    SequencerState& sequencer,
    uint8_t insertAt,
    uint8_t insertedLength
);
bool removeClipPatternSpan(
    SequencerState& sequencer,
    uint8_t removeAt,
    uint8_t removedLength
);
bool setClipPatternStepsPerBeat(
    SequencerState& sequencer,
    uint8_t stepsPerBeat
);

}  // namespace core::state::sequencer

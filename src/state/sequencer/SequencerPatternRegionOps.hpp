#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerPlaybackRegion.hpp>

namespace core::state::sequencer {

struct SequencerPatternState;

using SequencerPatternPlaybackRegion =
    oc::note::sequencer::StepSequencerPlaybackRegion;

/** Returns the exact persisted region without repairing invalid state. */
SequencerPatternPlaybackRegion patternPlaybackRegion(
    const SequencerPatternState& pattern
);

/**
 * Re-bounds a valid region to a new content length.
 *
 * Shrinking keeps every marker as close as possible to its former position.
 * Expanding extends Loop End only when it followed the previous Content Length.
 */
SequencerPatternPlaybackRegion resizedPatternPlaybackRegion(
    const SequencerPatternPlaybackRegion& region,
    uint8_t newContentLength
);

/** Shifts boundaries at/after an inserted span, then restores invariants. */
SequencerPatternPlaybackRegion insertedPatternPlaybackRegion(
    const SequencerPatternPlaybackRegion& region,
    uint8_t insertAt,
    uint8_t insertedLength
);

/** Collapses boundaries crossing a removed span, then restores invariants. */
SequencerPatternPlaybackRegion removedPatternPlaybackRegion(
    const SequencerPatternPlaybackRegion& region,
    uint8_t removeAt,
    uint8_t removedLength
);

/** Rejects invalid input and publishes length/markers as one timing mutation. */
bool setPatternPlaybackRegion(
    SequencerPatternState& pattern,
    const SequencerPatternPlaybackRegion& region
);

/** Resize Content Length and repair its playback region atomically. */
bool resizePatternContent(SequencerPatternState& pattern, uint8_t newContentLength);

/** Apply the region/length side of a structural insertion. */
bool insertPatternRegionSpan(
    SequencerPatternState& pattern,
    uint8_t insertAt,
    uint8_t insertedLength
);

/** Apply the region/length side of a structural removal. */
bool removePatternRegionSpan(
    SequencerPatternState& pattern,
    uint8_t removeAt,
    uint8_t removedLength
);

}  // namespace core::state::sequencer

#pragma once

#include <cstdint>

#include <oc/note/sequencer/StepSequencerPlaybackRegion.hpp>

#include "state/sequencer/SequencerCcLaneDomain.hpp"

namespace core::state::sequencer {

/** Exact authored span surrounding one audible playback ordinal. */
struct SequencerCcLaneProjectionSpan {
    uint32_t sourceOrdinal = 0;
    uint32_t targetOrdinal = 0;
    uint16_t distanceToTarget = 0;
    uint16_t elapsedAtOrdinal = 0;
    uint8_t sourceStep = 0;
    uint8_t targetStep = 0;
    SequencerCcLaneTransition transition = SequencerCcLaneTransition::HOLD;
    bool sourceValid = false;
    bool targetValid = false;
};

/**
 * Resolve the last audible source and its next target.
 *
 * Prelude events are considered only during the first traversal. Once the
 * loop has wrapped, projection stays strictly inside [Loop Start, Loop End).
 */
[[nodiscard]] bool resolveSequencerCcLaneProjectionSpan(
    const SequencerCcLane& lane,
    const oc::note::sequencer::StepSequencerPlaybackRegion& region,
    uint32_t playbackOrdinal,
    SequencerCcLaneProjectionSpan& out
);

/** Project one value. Returns false before the first audible authored event. */
[[nodiscard]] bool projectSequencerCcLaneValue(
    const SequencerCcLane& lane,
    const oc::note::sequencer::StepSequencerPlaybackRegion& region,
    uint32_t playbackOrdinal,
    float fractionInStep,
    uint8_t& outValue,
    SequencerCcLaneProjectionSpan* outSpan = nullptr
);

/** Stable UI occurrence: Prelude once, Loop on its second traversal. */
[[nodiscard]] bool representativeSequencerCcLaneOrdinalForStep(
    const oc::note::sequencer::StepSequencerPlaybackRegion& region,
    uint8_t step,
    uint32_t& outOrdinal
);

}  // namespace core::state::sequencer

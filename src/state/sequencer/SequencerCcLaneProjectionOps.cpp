#include "state/sequencer/SequencerCcLaneProjectionOps.hpp"

#include <algorithm>
#include <cstdint>

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

namespace {

constexpr uint8_t NO_EVENT = 128U;

// Search authored step ranges directly. Only the current playback position
// needs an ordinal/loop division, not every candidate before and after it.
FLASHMEM uint8_t firstEvent(const oc::note::sequencer::StepBitMask128& mask,
                          uint8_t begin, uint8_t end) {
    for (uint8_t step = begin; step < end; ++step) {
        if (mask.test(step)) return step;
    }
    return NO_EVENT;
}

FLASHMEM uint8_t lastEvent(const oc::note::sequencer::StepBitMask128& mask,
                         uint8_t begin, uint8_t end) {
    while (end > begin) {
        if (mask.test(--end)) return end;
    }
    return NO_EVENT;
}

}  // namespace

FLASHMEM bool resolveSequencerCcLaneProjectionSpan(
    const SequencerCcLane& lane,
    const oc::note::sequencer::StepSequencerPlaybackRegion& region,
    uint32_t playbackOrdinal,
    SequencerCcLaneProjectionSpan& out
) {
    if (!lane.occupied || !region.isValid()) return false;

    oc::note::sequencer::StepSequencerPlaybackPosition current{};
    if (!oc::note::sequencer::tryResolvePlaybackOrdinal(
            region,
            playbackOrdinal,
            current
        )) {
        return false;
    }

    SequencerCcLaneProjectionSpan span{};
    const bool repeatedLoop = !current.inPrelude && current.loopCycleIndex > 0U;
    uint8_t sourceStep = lastEvent(lane.activeMask,
        repeatedLoop ? region.loopStart : region.playStart, current.stepIndex + 1U);
    uint32_t sourceOrdinal = playbackOrdinal;
    if (sourceStep != NO_EVENT) {
        sourceOrdinal -= current.stepIndex - sourceStep;
    } else if (repeatedLoop) {
        sourceStep = lastEvent(lane.activeMask, current.stepIndex + 1U, region.loopEnd);
        if (sourceStep != NO_EVENT) {
            sourceOrdinal -= current.stepIndex - region.loopStart + region.loopEnd - sourceStep;
        } else {
            // An otherwise empty loop keeps the original Prelude CC held;
            // it does not replay that event on each wrap.
            sourceStep = lastEvent(lane.activeMask, region.playStart, region.loopStart);
            if (sourceStep != NO_EVENT) sourceOrdinal = sourceStep - region.playStart;
        }
    }
    if (sourceStep == NO_EVENT) return false;

    span.sourceValid = true;
    span.sourceStep = sourceStep;
    span.sourceOrdinal = sourceOrdinal;
    span.elapsedAtOrdinal = static_cast<uint16_t>(std::min<uint32_t>(
        playbackOrdinal - sourceOrdinal,
        UINT16_MAX
    ));
    span.transition = sequencerCcLaneTransition(lane, sourceStep);

    uint8_t targetStep = firstEvent(lane.activeMask, sourceStep + 1U, region.loopEnd);
    uint16_t distance = targetStep - sourceStep;
    if (targetStep == NO_EVENT && sourceStep >= region.loopStart) {
        targetStep = firstEvent(lane.activeMask, region.loopStart, sourceStep + 1U);
        distance = region.loopEnd - sourceStep + targetStep - region.loopStart;
    }
    if (targetStep != NO_EVENT && sourceOrdinal <= UINT32_MAX - distance) {
        span.targetValid = true;
        span.targetStep = targetStep;
        span.targetOrdinal = sourceOrdinal + distance;
        span.distanceToTarget = distance;
    }

    out = span;
    return true;
}

FLASHMEM bool projectSequencerCcLaneValue(
    const SequencerCcLane& lane,
    const oc::note::sequencer::StepSequencerPlaybackRegion& region,
    uint32_t playbackOrdinal,
    float fractionInStep,
    uint8_t& outValue,
    SequencerCcLaneProjectionSpan* outSpan
) {
    SequencerCcLaneProjectionSpan span{};
    if (!resolveSequencerCcLaneProjectionSpan(
            lane,
            region,
            playbackOrdinal,
            span
        )) {
        return false;
    }

    uint8_t value = lane.values[span.sourceStep];
    if (span.targetValid && span.distanceToTarget > 0U &&
        span.transition != SequencerCcLaneTransition::HOLD) {
        const float elapsed = static_cast<float>(span.elapsedAtOrdinal) +
            std::clamp(fractionInStep, 0.0f, 1.0f);
        const float progress = elapsed / static_cast<float>(span.distanceToTarget);
        value = interpolateSequencerCcLaneValue(
            lane.values[span.sourceStep],
            lane.values[span.targetStep],
            span.transition,
            progress
        );
    }

    outValue = value;
    if (outSpan != nullptr) *outSpan = span;
    return true;
}

FLASHMEM bool representativeSequencerCcLaneOrdinalForStep(
    const oc::note::sequencer::StepSequencerPlaybackRegion& region,
    uint8_t step,
    uint32_t& outOrdinal
) {
    if (!region.isValid() || step < region.playStart || step >= region.loopEnd) {
        return false;
    }
    if (step < region.loopStart) {
        outOrdinal = static_cast<uint32_t>(step - region.playStart);
        return true;
    }
    outOrdinal = static_cast<uint32_t>(region.preludeLength()) +
        region.loopLength() + static_cast<uint32_t>(step - region.loopStart);
    return true;
}

static_assert(sizeof(SequencerCcLaneProjectionSpan) <= 24U);

}  // namespace core::state::sequencer

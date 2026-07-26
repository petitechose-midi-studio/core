#include "state/sequencer/SequencerCcLaneProjectionOps.hpp"

#include <algorithm>
#include <cstdint>

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {

namespace {

FLASHMEM bool eventAtOrdinal(
    const SequencerCcLane& lane,
    const oc::note::sequencer::StepSequencerPlaybackRegion& region,
    uint32_t ordinal,
    uint8_t& outStep
) {
    oc::note::sequencer::StepSequencerPlaybackPosition position{};
    if (!oc::note::sequencer::tryResolvePlaybackOrdinal(region, ordinal, position)) {
        return false;
    }
    outStep = position.stepIndex;
    return lane.activeMask.test(position.stepIndex);
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
    uint8_t sourceStep = current.stepIndex;
    uint32_t sourceOrdinal = playbackOrdinal;
    bool sourceFound = lane.activeMask.test(current.stepIndex);

    const uint32_t maximumBack = current.inPrelude || current.loopCycleIndex == 0U
        ? playbackOrdinal
        : region.loopLength();
    for (uint32_t distance = 1U; !sourceFound && distance <= maximumBack; ++distance) {
        uint8_t candidateStep = 0;
        const uint32_t candidateOrdinal = playbackOrdinal - distance;
        if (!eventAtOrdinal(lane, region, candidateOrdinal, candidateStep)) continue;
        sourceStep = candidateStep;
        sourceOrdinal = candidateOrdinal;
        sourceFound = true;
    }
    // A Prelude CC is authored once but, like any MIDI CC, its held value may
    // remain audible until a Loop event replaces it. Do not wrap/retrigger the
    // Prelude; retain only its original occurrence as the historical source.
    if (!sourceFound && !current.inPrelude && current.loopCycleIndex > 0U) {
        for (int step = static_cast<int>(region.loopStart) - 1;
             step >= static_cast<int>(region.playStart);
             --step) {
            const uint8_t candidate = static_cast<uint8_t>(step);
            if (!lane.activeMask.test(candidate)) continue;
            sourceStep = candidate;
            sourceOrdinal = static_cast<uint32_t>(candidate - region.playStart);
            sourceFound = true;
            break;
        }
    }
    if (!sourceFound) return false;

    span.sourceValid = true;
    span.sourceStep = sourceStep;
    span.sourceOrdinal = sourceOrdinal;
    span.elapsedAtOrdinal = static_cast<uint16_t>(std::min<uint32_t>(
        playbackOrdinal - sourceOrdinal,
        UINT16_MAX
    ));
    span.transition = sequencerCcLaneTransition(lane, sourceStep);

    const bool sourceInPrelude = sourceStep < region.loopStart;
    const uint32_t maximumForward = sourceInPrelude
        ? static_cast<uint32_t>(region.preludeLength()) + region.loopLength() -
            sourceOrdinal - 1U
        : region.loopLength();
    for (uint32_t distance = 1U; distance <= maximumForward; ++distance) {
        if (sourceOrdinal > UINT32_MAX - distance) break;
        uint8_t candidateStep = 0;
        const uint32_t candidateOrdinal = sourceOrdinal + distance;
        if (!eventAtOrdinal(lane, region, candidateOrdinal, candidateStep)) continue;
        span.targetValid = true;
        span.targetStep = candidateStep;
        span.targetOrdinal = candidateOrdinal;
        span.distanceToTarget = static_cast<uint16_t>(distance);
        break;
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

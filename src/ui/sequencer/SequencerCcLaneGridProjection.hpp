#pragma once

#include <algorithm>
#include <cstdint>

#include <oc/note/sequencer/StepSequencerPlaybackRegion.hpp>

#include "state/sequencer/SequencerCcLaneProjectionOps.hpp"
#include "ui/sequencer/SequencerCcLaneGrid.hpp"

namespace core::ui::sequencer {

struct SequencerCcLaneProjectionSpan {
    bool valid = false;
    uint8_t source = 0;
    uint8_t target = 0;
    uint8_t distanceToTarget = 0;
    uint8_t elapsedAtStep = 0;
    core::state::sequencer::SequencerCcLaneTransition transition =
        core::state::sequencer::SequencerCcLaneTransition::HOLD;
};

[[nodiscard]] inline SequencerCcLaneProjectionSpan
sequencerCcLaneProjectionSpanAtStep(
    const core::state::sequencer::SequencerCcLane& lane,
    uint8_t step,
    const oc::note::sequencer::StepSequencerPlaybackRegion& region
) {
    SequencerCcLaneProjectionSpan result{};
    uint32_t ordinal = 0;
    if (!core::state::sequencer::representativeSequencerCcLaneOrdinalForStep(
            region,
            step,
            ordinal
        )) {
        return result;
    }
    core::state::sequencer::SequencerCcLaneProjectionSpan span{};
    if (!core::state::sequencer::resolveSequencerCcLaneProjectionSpan(
            lane,
            region,
            ordinal,
            span
        )) {
        return result;
    }
    result.valid = true;
    result.source = span.sourceStep;
    result.target = span.targetValid ? span.targetStep : span.sourceStep;
    result.distanceToTarget = static_cast<uint8_t>(std::min<uint16_t>(
        span.distanceToTarget,
        UINT8_MAX
    ));
    result.elapsedAtStep = static_cast<uint8_t>(std::min<uint16_t>(
        span.elapsedAtOrdinal,
        UINT8_MAX
    ));
    result.transition = span.transition;
    return result;
}

[[nodiscard]] inline SequencerCcLaneProjectionSpan
sequencerCcLaneProjectionSpanAtStep(
    const core::state::sequencer::SequencerCcLane& lane,
    uint8_t step,
    uint8_t length
) {
    return sequencerCcLaneProjectionSpanAtStep(
        lane,
        step,
        oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(length)
    );
}

[[nodiscard]] inline uint8_t projectSequencerCcLaneValueAtPosition(
    const core::state::sequencer::SequencerCcLane& lane,
    uint8_t step,
    float fraction,
    const oc::note::sequencer::StepSequencerPlaybackRegion& region
) {
    uint32_t ordinal = 0;
    uint8_t value = lane.initialValue;
    if (!core::state::sequencer::representativeSequencerCcLaneOrdinalForStep(
            region,
            step,
            ordinal
        )) {
        return value;
    }
    (void)core::state::sequencer::projectSequencerCcLaneValue(
        lane,
        region,
        ordinal,
        fraction,
        value
    );
    return value;
}

[[nodiscard]] inline uint8_t projectSequencerCcLaneValueAtPosition(
    const core::state::sequencer::SequencerCcLane& lane,
    uint8_t step,
    float fraction,
    uint8_t length
) {
    return projectSequencerCcLaneValueAtPosition(
        lane,
        step,
        fraction,
        oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(length)
    );
}

[[nodiscard]] inline core::ui::SequencerCcLaneGridCurveSegment
projectSequencerCcLaneGridSegment(
    const core::state::sequencer::SequencerCcLane& lane,
    uint8_t step,
    const oc::note::sequencer::StepSequencerPlaybackRegion& region
) {
    namespace seq = core::state::sequencer;
    core::ui::SequencerCcLaneGridCurveSegment segment{};
    if (!region.isValid() || step >= region.contentLength) return segment;
    const auto span = sequencerCcLaneProjectionSpanAtStep(lane, step, region);
    segment.visible = true;
    if (!span.valid || span.distanceToTarget == 0U) {
        segment.pointCount = 2;
        segment.points[0] = {.position = 0, .value = lane.initialValue};
        segment.points[1] = {.position = 255, .value = lane.initialValue};
        return segment;
    }

    const uint8_t startValue =
        projectSequencerCcLaneValueAtPosition(lane, step, 0.0f, region);
    if (span.transition == seq::SequencerCcLaneTransition::HOLD) {
        const uint8_t endValue =
            projectSequencerCcLaneValueAtPosition(lane, step, 1.0f, region);
        segment.pointCount = endValue == startValue ? 2U : 3U;
        segment.points[0] = {.position = 0, .value = startValue};
        segment.points[1] = {.position = 255, .value = startValue};
        if (segment.pointCount == 3U) {
            segment.points[2] = {.position = 255, .value = endValue};
        }
        return segment;
    }

    segment.pointCount = static_cast<uint8_t>(segment.points.size());
    for (uint8_t i = 0; i < segment.pointCount; ++i) {
        const float fraction = static_cast<float>(i) /
            static_cast<float>(segment.pointCount - 1U);
        segment.points[i] = {
            .position = static_cast<uint8_t>(
                (static_cast<uint16_t>(i) * 255U) /
                static_cast<uint16_t>(segment.pointCount - 1U)
            ),
            .value = projectSequencerCcLaneValueAtPosition(
                lane,
                step,
                fraction,
                region
            ),
        };
    }
    return segment;
}

[[nodiscard]] inline core::ui::SequencerCcLaneGridCurveSegment
projectSequencerCcLaneGridSegment(
    const core::state::sequencer::SequencerCcLane& lane,
    uint8_t step,
    uint8_t length
) {
    return projectSequencerCcLaneGridSegment(
        lane,
        step,
        oc::note::sequencer::StepSequencerPlaybackRegion::fullLength(length)
    );
}

}  // namespace core::ui::sequencer

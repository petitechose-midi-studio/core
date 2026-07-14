#pragma once

#include <algorithm>
#include <cstdint>

#include "state/sequencer/SequencerCcLaneDomain.hpp"
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
    uint8_t length
) {
    SequencerCcLaneProjectionSpan span{};
    if (length == 0U || step >= length) return span;
    uint8_t source = step;
    for (uint16_t distance = 0; distance < length; ++distance) {
        const uint8_t candidate = static_cast<uint8_t>(
            (static_cast<uint16_t>(step) + length - distance) % length
        );
        if (!lane.activeMask.test(candidate)) continue;
        source = candidate;
        span.valid = true;
        break;
    }
    if (!span.valid) return span;

    uint8_t target = source;
    for (uint16_t distance = 1; distance <= length; ++distance) {
        const uint8_t candidate = static_cast<uint8_t>(
            (static_cast<uint16_t>(source) + distance) % length
        );
        if (!lane.activeMask.test(candidate)) continue;
        target = candidate;
        span.distanceToTarget = static_cast<uint8_t>(distance);
        break;
    }
    span.source = source;
    span.target = target;
    span.elapsedAtStep = static_cast<uint8_t>(
        (static_cast<uint16_t>(step) + length - source) % length
    );
    span.transition =
        core::state::sequencer::sequencerCcLaneTransition(lane, source);
    return span;
}

[[nodiscard]] inline uint8_t projectSequencerCcLaneValueAtPosition(
    const core::state::sequencer::SequencerCcLane& lane,
    uint8_t step,
    float fraction,
    uint8_t length
) {
    const auto span = sequencerCcLaneProjectionSpanAtStep(lane, step, length);
    if (!span.valid || span.distanceToTarget == 0U) return lane.initialValue;
    const float progress = (
        static_cast<float>(span.elapsedAtStep) +
        std::clamp(fraction, 0.0f, 1.0f)
    ) / static_cast<float>(span.distanceToTarget);
    if (progress >= 1.0f) return lane.values[span.target];
    return core::state::sequencer::interpolateSequencerCcLaneValue(
        lane.values[span.source],
        lane.values[span.target],
        span.transition,
        progress
    );
}

[[nodiscard]] inline core::ui::SequencerCcLaneGridCurveSegment
projectSequencerCcLaneGridSegment(
    const core::state::sequencer::SequencerCcLane& lane,
    uint8_t step,
    uint8_t length
) {
    namespace seq = core::state::sequencer;
    core::ui::SequencerCcLaneGridCurveSegment segment{};
    if (length == 0U || step >= length) return segment;
    const auto span = sequencerCcLaneProjectionSpanAtStep(lane, step, length);
    segment.visible = true;
    if (!span.valid || span.distanceToTarget == 0U) {
        segment.pointCount = 2;
        segment.points[0] = {.position = 0, .value = lane.initialValue};
        segment.points[1] = {.position = 255, .value = lane.initialValue};
        return segment;
    }

    const uint8_t startValue =
        projectSequencerCcLaneValueAtPosition(lane, step, 0.0f, length);
    if (span.transition == seq::SequencerCcLaneTransition::HOLD) {
        const uint8_t endValue =
            projectSequencerCcLaneValueAtPosition(lane, step, 1.0f, length);
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
                length
            ),
        };
    }
    return segment;
}

}  // namespace core::ui::sequencer

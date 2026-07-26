#include "state/modulation/ProjectRecordedShapeTake.hpp"

#include <algorithm>
#include <cstdint>
#include <limits>

#include <config/PlatformCompat.hpp>

namespace core::state::modulation {

namespace {

constexpr int32_t kSimplificationErrorQ15 = 1;

FLASHMEM int16_t clampSource(int64_t value) {
    return static_cast<int16_t>(std::clamp<int64_t>(
        value,
        ProjectRecordedShapeTake::SOURCE_MIN,
        ProjectRecordedShapeTake::SOURCE_MAX
    ));
}

FLASHMEM int16_t interpolateValue(int16_t left,
                         int16_t right,
                         uint32_t offset,
                         uint32_t span) {
    if (span == 0U || offset == 0U) return left;
    if (offset >= span) return right;
    const int64_t delta = static_cast<int64_t>(right) - left;
    int64_t numerator = delta * offset;
    numerator += numerator >= 0
        ? static_cast<int64_t>(span / 2U)
        : -static_cast<int64_t>(span / 2U);
    return clampSource(static_cast<int64_t>(left) + numerator / span);
}

FLASHMEM bool validPrefill(const ProjectCurveSpec& source,
                           const ProjectPackedCurvePoint* points,
                           uint16_t pointCount) {
    if (points == nullptr || pointCount == 0U ||
        source.sourceDurationTicks == 0U || source.durationTicks == 0U ||
        source.windowOffsetTicks > source.sourceDurationTicks ||
        source.interpolation != ProjectCurveInterpolation::LINEAR ||
        source.valueDomain != ProjectCurveValueDomain::BIPOLAR ||
        static_cast<uint8_t>(source.origin) >
            static_cast<uint8_t>(ProjectCurveOrigin::CONVERTED_MIN)) {
        return false;
    }
    for (uint16_t index = 0U; index < pointCount; ++index) {
        if (points[index].tick > source.sourceDurationTicks ||
            points[index].value == std::numeric_limits<int16_t>::min() ||
            (index > 0U && points[index - 1U].tick > points[index].tick)) {
            return false;
        }
    }
    return true;
}

struct PrefillEvaluationCursor {
    uint32_t searchSteps = 0U;
    uint16_t right = 1U;
    uint16_t lastTick = 0U;
    bool initialized = false;
};

FLASHMEM uint16_t upperBoundPrefill(
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount,
    uint16_t tick,
    uint32_t& searchSteps
) {
    uint16_t first = 1U;
    uint16_t last = pointCount;
    while (first < last) {
        ++searchSteps;
        const uint16_t middle = static_cast<uint16_t>(
            first + (last - first) / 2U
        );
        if (points[middle].tick <= tick) first = middle + 1U;
        else last = middle;
    }
    return first;
}

FLASHMEM int16_t evaluatePrefill(
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount,
    uint16_t tick,
    bool binaryReset,
    PrefillEvaluationCursor& cursor
) {
    if (pointCount == 1U || tick <= points[0].tick) {
        cursor.right = binaryReset && pointCount > 1U
            ? upperBoundPrefill(
                  points,
                  pointCount,
                  tick,
                  cursor.searchSteps
              )
            : 1U;
        cursor.lastTick = tick;
        cursor.initialized = true;
        return points[0].value;
    }
    const bool wrapped = cursor.initialized && tick < cursor.lastTick;
    if (!cursor.initialized || wrapped) {
        cursor.right = wrapped && binaryReset
            ? upperBoundPrefill(
                  points,
                  pointCount,
                  tick,
                  cursor.searchSteps
              )
            : 1U;
    }
    while (cursor.right < pointCount) {
        ++cursor.searchSteps;
        if (points[cursor.right].tick > tick) break;
        ++cursor.right;
    }
    const uint16_t right = cursor.right;
    cursor.lastTick = tick;
    cursor.initialized = true;
    if (right >= pointCount) {
        return points[static_cast<uint16_t>(pointCount - 1U)].value;
    }
    const auto& leftPoint = points[static_cast<uint16_t>(right - 1U)];
    const auto& rightPoint = points[right];
    if (rightPoint.tick == leftPoint.tick) return rightPoint.value;
    return interpolateValue(
        leftPoint.value,
        rightPoint.value,
        static_cast<uint32_t>(tick - leftPoint.tick),
        static_cast<uint32_t>(rightPoint.tick - leftPoint.tick)
    );
}

FLASHMEM bool appendPoint(ProjectPackedCurvePoint* output,
                          uint16_t capacity,
                          uint16_t& written,
                          ProjectPackedCurvePoint point) {
    if (written >= capacity) return false;
    output[written++] = point;
    return true;
}

}  // namespace

FLASHMEM void ProjectRecordedShapeTake::reset() {
    values.fill(0);
    startProjectPhaseTick = 0U;
    latestElapsedTick = 0U;
    lastWriteElapsedTick = 0U;
    scratchCurveRevision = 0U;
    prefillSearchSteps = 0U;
    durationTicks = 0U;
    sampleCount = 0U;
    currentValue = 0;
    lastWriteValue = 0;
    phase = ProjectRecordedShapeTakePhase::IDLE;
    touched = false;
    changed = false;
    reduced = false;
    prefilled = false;
    writeCursor = false;
}

FLASHMEM bool ProjectRecordedShapeTake::begin(
    uint16_t duration,
    uint32_t projectPhaseTick
) {
    reset();
    if (duration == 0U) return false;
    durationTicks = duration;
    startProjectPhaseTick = projectPhaseTick;
    const uint32_t requested = static_cast<uint32_t>(duration) + 1U;
    sampleCount = static_cast<uint16_t>(std::min<uint32_t>(
        requested,
        SAMPLE_CAPACITY
    ));
    if (sampleCount < 2U) sampleCount = 2U;
    reduced = requested > SAMPLE_CAPACITY;
    phase = ProjectRecordedShapeTakePhase::RECORDING;
    return true;
}

FLASHMEM bool ProjectRecordedShapeTake::prefill(
    const ProjectCurveSpec& source,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
) {
    if (phase != ProjectRecordedShapeTakePhase::RECORDING || writeCursor ||
        touched || !validPrefill(source, points, pointCount) ||
        durationTicks == 0U || sampleCount < 2U) {
        return false;
    }
    const uint16_t intervals = static_cast<uint16_t>(sampleCount - 1U);
    PrefillEvaluationCursor cursor{};
    // Canonical playback windows are no longer than their source. They have
    // at most one source-domain wrap plus the circular endpoint, so monotone
    // cursor resets remain O(samples + points). Defensive non-canonical long
    // windows use a logarithmic reset instead of rescanning a dense point array
    // at every wrap.
    const bool binaryReset = source.durationTicks >
        source.sourceDurationTicks;
    for (uint16_t sampleIndex = 0U;
         sampleIndex <= intervals;
         ++sampleIndex) {
        const uint16_t targetTick = gridTick_(sampleIndex);
        const uint16_t sourceTick = static_cast<uint16_t>(
            (static_cast<uint32_t>(source.windowOffsetTicks) +
             targetTick % source.durationTicks) %
            source.sourceDurationTicks
        );
        values[sampleIndex] = evaluatePrefill(
            points,
            pointCount,
            sourceTick,
            binaryReset,
            cursor
        );
    }
    prefillSearchSteps = cursor.searchSteps;
    // One storage cell mirrors phase zero so every wrap is deterministic.
    values[intervals] = values[0U];
    prefilled = true;
    return true;
}

FLASHMEM bool ProjectRecordedShapeTake::samplePreviewValue(
    uint16_t positionQ16,
    int16_t& value
) const {
    if (phase != ProjectRecordedShapeTakePhase::RECORDING ||
        sampleCount < 2U) {
        return false;
    }
    const uint16_t intervals = static_cast<uint16_t>(sampleCount - 1U);
    const uint32_t scaled = static_cast<uint32_t>(positionQ16) * intervals;
    const uint16_t lower = static_cast<uint16_t>(scaled / 65535U);
    const uint16_t upper = std::min<uint16_t>(
        static_cast<uint16_t>(lower + 1U),
        intervals
    );
    value = interpolateValue(
        values[lower],
        values[upper],
        scaled % 65535U,
        65535U
    );
    return true;
}

FLASHMEM bool ProjectRecordedShapeTake::touchDelta(
    int32_t deltaQ15,
    uint32_t elapsedTick
) {
    if (phase != ProjectRecordedShapeTakePhase::RECORDING) return false;
    const uint32_t monotoneElapsed = std::max(elapsedTick, latestElapsedTick);
    if (deltaQ15 == 0) {
        latestElapsedTick = monotoneElapsed;
        return true;
    }
    if (!writeCursor) currentValue = valueAtElapsed_(monotoneElapsed);
    const int16_t next = clampSource(
        static_cast<int64_t>(currentValue) + deltaQ15
    );
    if (!writeCursor && next == currentValue) {
        latestElapsedTick = monotoneElapsed;
        return true;
    }
    touched = true;
    currentValue = next;
    if (!sampleValue_(monotoneElapsed, currentValue)) return false;
    latestElapsedTick = monotoneElapsed;
    return true;
}

FLASHMEM bool ProjectRecordedShapeTake::sample(uint32_t elapsedTick) {
    if (phase != ProjectRecordedShapeTakePhase::RECORDING) return false;
    const uint32_t monotoneElapsed = std::max(elapsedTick, latestElapsedTick);
    if (writeCursor && !sampleValue_(monotoneElapsed, currentValue)) {
        return false;
    }
    latestElapsedTick = monotoneElapsed;
    return true;
}

FLASHMEM bool ProjectRecordedShapeTake::writePositionQ16(
    uint16_t& positionQ16
) const {
    if (phase != ProjectRecordedShapeTakePhase::RECORDING ||
        durationTicks == 0U) {
        return false;
    }
    const uint32_t phaseTick = static_cast<uint32_t>(
        (static_cast<uint64_t>(startProjectPhaseTick % durationTicks) +
         latestElapsedTick % durationTicks) % durationTicks
    );
    positionQ16 = static_cast<uint16_t>(
        (static_cast<uint64_t>(phaseTick) * 65535U + durationTicks / 2U) /
        durationTicks
    );
    return true;
}

FLASHMEM bool ProjectRecordedShapeTake::buildPackedCurve(
    ProjectCurveSpec& spec,
    ProjectPackedCurvePoint* output,
    uint16_t capacity,
    uint16_t& written
) const {
    written = 0U;
    if (phase != ProjectRecordedShapeTakePhase::RECORDING || !touched ||
        !changed || output == nullptr || capacity == 0U ||
        durationTicks == 0U || sampleCount < 2U) {
        return false;
    }

    uint16_t outCount = 0U;
    if (!appendPoint(
            output,
            capacity,
            outCount,
            ProjectPackedCurvePoint{0U, values[0U]}
        )) {
        return false;
    }

    const uint16_t lastIndex = static_cast<uint16_t>(sampleCount - 1U);
    uint16_t anchor = 0U;
    uint16_t candidate = 1U;
    double lowerSlope = -std::numeric_limits<double>::infinity();
    double upperSlope = std::numeric_limits<double>::infinity();
    while (candidate < sampleCount) {
        const uint16_t anchorTick = gridTick_(anchor);
        const uint16_t candidateTick = gridTick_(candidate);
        const int32_t span = static_cast<int32_t>(candidateTick) - anchorTick;
        if (span <= 0) {
            written = 0U;
            return false;
        }
        const double delta = static_cast<double>(
            static_cast<int32_t>(values[candidate]) - values[anchor]
        );
        const double slope = delta / static_cast<double>(span);
        if (candidate == static_cast<uint16_t>(anchor + 1U) ||
            (slope >= lowerSlope && slope <= upperSlope)) {
            lowerSlope = std::max(
                lowerSlope,
                (delta - kSimplificationErrorQ15) /
                    static_cast<double>(span)
            );
            upperSlope = std::min(
                upperSlope,
                (delta + kSimplificationErrorQ15) /
                    static_cast<double>(span)
            );
            ++candidate;
            continue;
        }
        const uint16_t keep = static_cast<uint16_t>(candidate - 1U);
        if (!appendPoint(
                output,
                capacity,
                outCount,
                ProjectPackedCurvePoint{gridTick_(keep), values[keep]}
            )) {
            written = 0U;
            return false;
        }
        anchor = keep;
        candidate = static_cast<uint16_t>(anchor + 1U);
        lowerSlope = -std::numeric_limits<double>::infinity();
        upperSlope = std::numeric_limits<double>::infinity();
    }

    const ProjectPackedCurvePoint last{
        durationTicks,
        values[lastIndex],
    };
    const auto& previous = output[static_cast<uint16_t>(outCount - 1U)];
    if (previous.tick != last.tick || previous.value != last.value) {
        if (!appendPoint(output, capacity, outCount, last)) {
            written = 0U;
            return false;
        }
    }

    spec = ProjectCurveSpec{
        .sourceDurationTicks = durationTicks,
        .durationTicks = durationTicks,
        .windowOffsetTicks = 0U,
        .interpolation = ProjectCurveInterpolation::LINEAR,
        .valueDomain = ProjectCurveValueDomain::BIPOLAR,
        .origin = ProjectCurveOrigin::NATIVE,
    };
    written = outCount;
    return written > 0U;
}

FLASHMEM uint16_t ProjectRecordedShapeTake::gridTick_(
    uint16_t sampleIndex
) const {
    if (sampleCount < 2U) return 0U;
    const uint16_t intervals = static_cast<uint16_t>(sampleCount - 1U);
    const uint16_t bounded = std::min(sampleIndex, intervals);
    return static_cast<uint16_t>(
        (static_cast<uint32_t>(bounded) * durationTicks + intervals / 2U) /
        intervals
    );
}

FLASHMEM int16_t ProjectRecordedShapeTake::valueAtElapsed_(
    uint32_t elapsedTick
) const {
    if (durationTicks == 0U || sampleCount < 2U) return 0;
    const uint16_t intervals = static_cast<uint16_t>(sampleCount - 1U);
    const uint32_t phaseTick = static_cast<uint32_t>(
        (static_cast<uint64_t>(startProjectPhaseTick % durationTicks) +
         elapsedTick % durationTicks) % durationTicks
    );
    const uint64_t scaled = static_cast<uint64_t>(phaseTick) * intervals;
    const uint16_t lower = static_cast<uint16_t>(scaled / durationTicks);
    const uint16_t upper = std::min<uint16_t>(
        static_cast<uint16_t>(lower + 1U),
        intervals
    );
    return interpolateValue(
        values[lower],
        values[upper],
        static_cast<uint32_t>(scaled % durationTicks),
        durationTicks
    );
}

FLASHMEM void ProjectRecordedShapeTake::writeGridValue_(
    uint64_t absoluteGridOrdinal,
    int16_t value
) {
    if (sampleCount < 2U) return;
    const uint16_t intervals = static_cast<uint16_t>(sampleCount - 1U);
    const uint16_t sampleIndex = static_cast<uint16_t>(
        absoluteGridOrdinal % intervals
    );
    const int16_t bounded = clampSource(value);
    const bool endpointChanged = sampleIndex == 0U &&
        values[intervals] != bounded;
    if (values[sampleIndex] != bounded || endpointChanged) {
        changed = true;
        ++scratchCurveRevision;
    }
    values[sampleIndex] = bounded;
    if (sampleIndex == 0U) values[intervals] = bounded;
}

FLASHMEM bool ProjectRecordedShapeTake::sampleValue_(
    uint32_t elapsedTick,
    int16_t value
) {
    if (sampleCount < 2U || durationTicks == 0U) return false;
    const uint16_t intervals = static_cast<uint16_t>(sampleCount - 1U);
    const uint64_t absoluteTick =
        static_cast<uint64_t>(startProjectPhaseTick) + elapsedTick;
    const uint64_t currentOrdinal =
        (absoluteTick * intervals) / durationTicks;
    if (!writeCursor) {
        writeGridValue_(currentOrdinal, value);
        lastWriteElapsedTick = elapsedTick;
        lastWriteValue = value;
        writeCursor = true;
        return true;
    }
    if (elapsedTick < lastWriteElapsedTick) return false;

    const uint64_t previousTick =
        static_cast<uint64_t>(startProjectPhaseTick) + lastWriteElapsedTick;
    const uint64_t previousOrdinal =
        (previousTick * intervals) / durationTicks;
    uint64_t firstOrdinal = previousOrdinal;
    if (currentOrdinal > firstOrdinal + intervals) {
        firstOrdinal = currentOrdinal - intervals;
    }
    const uint32_t span = elapsedTick - lastWriteElapsedTick;
    for (uint64_t ordinal = firstOrdinal + 1U;
         ordinal <= currentOrdinal;
         ++ordinal) {
        const uint64_t crossedAbsoluteTick =
            (ordinal * durationTicks + intervals - 1U) / intervals;
        const uint64_t crossedElapsed = crossedAbsoluteTick >
                startProjectPhaseTick
            ? crossedAbsoluteTick - startProjectPhaseTick
            : 0U;
        const uint32_t relative = span == 0U
            ? 0U
            : static_cast<uint32_t>(std::min<uint64_t>(
                  crossedElapsed > lastWriteElapsedTick
                      ? crossedElapsed - lastWriteElapsedTick
                      : 0U,
                  span
              ));
        writeGridValue_(
            ordinal,
            interpolateValue(lastWriteValue, value, relative, span)
        );
    }
    writeGridValue_(currentOrdinal, value);
    lastWriteElapsedTick = elapsedTick;
    lastWriteValue = value;
    return true;
}

}  // namespace core::state::modulation

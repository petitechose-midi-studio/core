#include "state/macro/MacroAutomationDomain.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

namespace core::state::macro {

namespace {

constexpr std::array<float, 5> kShortDurationTableBeats{
    0.25f,
    0.5f,
    1.0f,
    2.0f,
    4.0f,
};
constexpr float kBarBeats = 4.0f;
constexpr float kMaxDurationBeats =
    static_cast<float>(UINT16_MAX) / static_cast<float>(MACRO_AUTOMATION_TICKS_PER_BEAT);
constexpr int16_t kPackedValueMax = 32767;
constexpr float kAutomationSimplifyMaxError = 0.5f / 127.0f;

FLASHMEM float sanitizeDuration(float durationBeats) {
    if (!std::isfinite(durationBeats) || durationBeats <= 0.0f) {
        return 1.0f;
    }
    return std::min(durationBeats, kMaxDurationBeats);
}

FLASHMEM float wrapBeat(float beat, float durationBeats) {
    const float duration = sanitizeDuration(durationBeats);
    if (!std::isfinite(beat)) return 0.0f;
    float wrapped = std::fmod(beat, duration);
    if (wrapped < 0.0f) {
        wrapped += duration;
    }
    return wrapped;
}

FLASHMEM uint16_t offsetTicksFromBeats(float beats) {
    if (!std::isfinite(beats) || beats <= 0.0f) return 0;
    const float ticks =
        std::min(beats, kMaxDurationBeats) *
        static_cast<float>(MACRO_AUTOMATION_TICKS_PER_BEAT);
    return static_cast<uint16_t>(std::clamp(
        static_cast<int>(std::lround(ticks)),
        0,
        static_cast<int>(UINT16_MAX)
    ));
}

FLASHMEM float interpolateLinear(float startBeat,
                                 float startValue,
                                 float endBeat,
                                 float endValue,
                                 float beat) {
    const float span = endBeat - startBeat;
    if (span <= 0.000001f) {
        return startValue;
    }
    const float t = std::clamp((beat - startBeat) / span, 0.0f, 1.0f);
    return startValue + ((endValue - startValue) * t);
}

template <typename PointContainer>
FLASHMEM float evaluatePoints(const PointContainer& points,
                              uint16_t pointCount,
                              float durationBeats,
                              float beat,
                              float fallbackValue,
                              bool signedOutput) {
    if (pointCount == 0) return fallbackValue;

    const uint16_t count = std::min<uint16_t>(pointCount, MACRO_AUTOMATION_RECORDING_MAX_POINTS);
    if (count == 1) {
        return signedOutput ? macroAutomationClampSigned(points[0].value)
                            : macroAutomationClamp01(points[0].value);
    }

    const float duration = sanitizeDuration(durationBeats);
    const float wrapped = wrapBeat(beat, duration);

    const auto& first = points[0];
    if (wrapped <= first.beat) {
        return signedOutput ? macroAutomationClampSigned(first.value)
                            : macroAutomationClamp01(first.value);
    }

    for (uint16_t i = 1; i < count; ++i) {
        const auto& previous = points[i - 1];
        const auto& current = points[i];
        if (wrapped <= current.beat) {
            const float value = interpolateLinear(
                previous.beat,
                previous.value,
                current.beat,
                current.value,
                wrapped
            );
            return signedOutput ? macroAutomationClampSigned(value)
                                : macroAutomationClamp01(value);
        }
    }

    const auto& last = points[count - 1];
    return signedOutput ? macroAutomationClampSigned(last.value)
                        : macroAutomationClamp01(last.value);
}

FLASHMEM MacroCurvePoint unpackPoolPoint(const MacroAutomationPointPool& pool,
                                         uint16_t index,
                                         bool signedOutput) {
    if (index >= pool.used) return {};
    const auto& packed = pool.points[index];
    return MacroCurvePoint{
        macroAutomationBeatsFromTicks(packed.tick),
        macroAutomationUnpackValue(packed.value, signedOutput),
    };
}

FLASHMEM uint16_t availablePoolPointCount(const MacroAutomationCurveRef& curve,
                                          const MacroAutomationPointPool& pool) {
    if (curve.pointOffset >= pool.used) return 0;
    const uint16_t available = static_cast<uint16_t>(pool.used - curve.pointOffset);
    return std::min<uint16_t>(curve.pointCount, available);
}

FLASHMEM uint16_t lastPointTick(const MacroAutomationCurveRef& curve,
                                const MacroAutomationPointPool& pool) {
    const uint16_t count = availablePoolPointCount(curve, pool);
    if (count == 0) return 0;
    return pool.points[static_cast<uint16_t>(curve.pointOffset + count - 1U)].tick;
}

FLASHMEM uint16_t sourceDurationTicks(const MacroAutomationCurveRef& curve,
                                      const MacroAutomationPointPool& pool) {
    return std::max<uint16_t>({
        curve.sourceDurationTicks,
        lastPointTick(curve, pool),
        1U,
    });
}

FLASHMEM uint16_t wrapSourceTick(uint32_t tick, uint16_t sourceTicks) {
    if (sourceTicks == 0) return 0;
    return static_cast<uint16_t>(tick % sourceTicks);
}

FLASHMEM uint16_t beatToWrappedTick(float beat, uint16_t durationTicks) {
    const uint16_t duration = durationTicks == 0U
        ? MACRO_AUTOMATION_TICKS_PER_BEAT
        : durationTicks;
    if (!std::isfinite(beat)) return 0;
    int32_t rounded = static_cast<int32_t>(std::lround(
        beat * static_cast<float>(MACRO_AUTOMATION_TICKS_PER_BEAT)
    ));
    const int32_t modulo = static_cast<int32_t>(duration);
    rounded %= modulo;
    if (rounded < 0) {
        rounded += modulo;
    }
    return static_cast<uint16_t>(rounded);
}

FLASHMEM float packedCurveValue(const MacroAutomationPointPool& pool,
                                uint16_t index,
                                bool signedOutput) {
    if (index >= pool.used) return 0.0f;
    return macroAutomationUnpackValue(pool.points[index].value, signedOutput);
}

FLASHMEM uint16_t lowerBoundPoolPointByTick(const MacroAutomationPointPool& pool,
                                            uint16_t start,
                                            uint16_t count,
                                            uint16_t tick) {
    uint16_t low = 0;
    uint16_t high = count;
    while (low < high) {
        const uint16_t mid = static_cast<uint16_t>(low + ((high - low) / 2U));
        const uint16_t poolIndex = static_cast<uint16_t>(start + mid);
        if (pool.points[poolIndex].tick < tick) {
            low = static_cast<uint16_t>(mid + 1U);
        } else {
            high = mid;
        }
    }
    return low;
}

FLASHMEM float evaluatePoolPointsAtSourceTick(const MacroAutomationCurveRef& curve,
                                              const MacroAutomationPointPool& pool,
                                              uint16_t sourceTick,
                                              float fallbackValue,
                                              bool signedOutput) {
    if (curve.pointCount == 0) return fallbackValue;
    if (curve.pointOffset >= pool.used) return fallbackValue;
    const uint16_t count = availablePoolPointCount(curve, pool);
    if (count == 0) return fallbackValue;

    if (count == 1) {
        return packedCurveValue(pool, curve.pointOffset, signedOutput);
    }

    const auto& first = pool.points[curve.pointOffset];
    const float firstValue = packedCurveValue(pool, curve.pointOffset, signedOutput);
    if (sourceTick <= first.tick) {
        return firstValue;
    }

    const uint16_t relativeIndex =
        lowerBoundPoolPointByTick(pool, curve.pointOffset, count, sourceTick);
    if (relativeIndex >= count) {
        const uint16_t lastIndex = static_cast<uint16_t>(curve.pointOffset + count - 1U);
        return packedCurveValue(pool, lastIndex, signedOutput);
    }

    const uint16_t previousIndex =
        static_cast<uint16_t>(curve.pointOffset + relativeIndex - 1U);
    const uint16_t currentIndex = static_cast<uint16_t>(curve.pointOffset + relativeIndex);
    const auto& previous = pool.points[previousIndex];
    const auto& current = pool.points[currentIndex];
    const float previousValue = macroAutomationUnpackValue(previous.value, signedOutput);
    const float currentValue = macroAutomationUnpackValue(current.value, signedOutput);
    const uint16_t span = static_cast<uint16_t>(current.tick - previous.tick);
    const float value = span == 0U
        ? currentValue
        : previousValue + (
            (currentValue - previousValue) *
            (static_cast<float>(sourceTick - previous.tick) / static_cast<float>(span))
        );
    return signedOutput ? macroAutomationClampSigned(value)
                        : macroAutomationClamp01(value);
}

FLASHMEM float evaluatePoolPoints(const MacroAutomationCurveRef& curve,
                                  const MacroAutomationPointPool& pool,
                                  float beat,
                                  float fallbackValue,
                                  bool signedOutput) {
    if (curve.pointCount == 0) return fallbackValue;
    if (curve.pointOffset >= pool.used) return fallbackValue;

    const uint16_t activeTicks = curve.durationTicks == 0
        ? MACRO_AUTOMATION_TICKS_PER_BEAT
        : curve.durationTicks;
    const uint16_t localTick = beatToWrappedTick(beat, activeTicks);
    const uint16_t sourceTicks = sourceDurationTicks(curve, pool);
    const uint16_t offsetTick = wrapSourceTick(curve.windowOffsetTicks, sourceTicks);
    const uint16_t sourceTick = wrapSourceTick(
        static_cast<uint32_t>(offsetTick) + localTick,
        sourceTicks
    );
    return evaluatePoolPointsAtSourceTick(curve, pool, sourceTick, fallbackValue, signedOutput);
}

FLASHMEM void remapLaneToDuration(MacroAutomationLane& lane,
                                  float rawDurationBeats,
                                  float targetDurationBeats) {
    const float raw = sanitizeDuration(rawDurationBeats);
    const float target = sanitizeDuration(targetDurationBeats);
    const float scale = target / raw;
    const uint16_t count =
        std::min<uint16_t>(lane.pointCount, MACRO_AUTOMATION_RECORDING_MAX_POINTS);
    for (uint16_t i = 0; i < count; ++i) {
        lane.points[i].beat = std::clamp(lane.points[i].beat * scale, 0.0f, target);
        lane.points[i].value = macroAutomationClamp01(lane.points[i].value);
    }
    lane.durationBeats = target;
    lane.active = true;
}

FLASHMEM uint16_t clampedLaneTick(float beat, uint16_t durationTicks) {
    if (!std::isfinite(beat) || beat <= 0.0f) return 0;
    const float ticks =
        beat * static_cast<float>(MACRO_AUTOMATION_TICKS_PER_BEAT);
    return static_cast<uint16_t>(std::clamp(
        static_cast<int>(std::lround(ticks)),
        0,
        static_cast<int>(durationTicks)
    ));
}

FLASHMEM void snapLaneToTickGrid(MacroAutomationLane& lane) {
    const uint16_t count =
        std::min<uint16_t>(lane.pointCount, MACRO_AUTOMATION_RECORDING_MAX_POINTS);
    if (count == 0) {
        lane.active = false;
        lane.pointCount = 0;
        return;
    }

    const uint16_t durationTicks = macroAutomationTicksFromBeats(lane.durationBeats);
    uint16_t written = 0;
    uint16_t lastTick = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const uint16_t tick = clampedLaneTick(lane.points[i].beat, durationTicks);
        const MacroCurvePoint snapped{
            macroAutomationBeatsFromTicks(tick),
            macroAutomationClamp01(lane.points[i].value),
        };

        if (written > 0 && tick == lastTick) {
            lane.points[static_cast<uint16_t>(written - 1U)] = snapped;
            continue;
        }

        lane.points[written] = snapped;
        lastTick = tick;
        ++written;
    }

    lane.pointCount = written;
    lane.active = written > 0;
}

FLASHMEM bool segmentFitsAutomationError(const MacroAutomationLane& lane,
                                         uint16_t start,
                                         uint16_t end,
                                         float maxError) {
    if (end <= static_cast<uint16_t>(start + 1U)) return true;
    const auto& first = lane.points[start];
    const auto& last = lane.points[end];
    for (uint16_t i = static_cast<uint16_t>(start + 1U); i < end; ++i) {
        const auto& point = lane.points[i];
        const float expected = interpolateLinear(
            first.beat,
            first.value,
            last.beat,
            last.value,
            point.beat
        );
        if (std::fabs(point.value - expected) > maxError) return false;
    }
    return true;
}

FLASHMEM void simplifyLaneByLinearError(MacroAutomationLane& lane) {
    const uint16_t count =
        std::min<uint16_t>(lane.pointCount, MACRO_AUTOMATION_RECORDING_MAX_POINTS);
    if (count <= 2) return;

    uint16_t write = 1;
    uint16_t anchor = 0;
    uint16_t candidate = 2;
    while (candidate < count) {
        if (segmentFitsAutomationError(
                lane,
                anchor,
                candidate,
                kAutomationSimplifyMaxError
            )) {
            ++candidate;
            continue;
        }

        const uint16_t keep = static_cast<uint16_t>(candidate - 1U);
        if (write != keep) {
            lane.points[write] = lane.points[keep];
        }
        ++write;
        anchor = keep;
        candidate = static_cast<uint16_t>(anchor + 2U);
    }

    const auto& last = lane.points[static_cast<uint16_t>(count - 1U)];
    if (write == 0 ||
        lane.points[static_cast<uint16_t>(write - 1U)].beat != last.beat ||
        lane.points[static_cast<uint16_t>(write - 1U)].value != last.value) {
        lane.points[write] = last;
        ++write;
    }

    lane.pointCount = write;
    lane.active = write > 0;
}

FLASHMEM void decimateLaneForContinuedRecording(MacroAutomationLane& lane) {
    const uint16_t count =
        std::min<uint16_t>(lane.pointCount, MACRO_AUTOMATION_RECORDING_MAX_POINTS);
    if (count <= 2) return;

    uint16_t write = 1;
    for (uint16_t read = 2; static_cast<uint16_t>(read + 1U) < count; read += 2U) {
        lane.points[write++] = lane.points[read];
    }
    lane.points[write++] = lane.points[static_cast<uint16_t>(count - 1U)];
    lane.pointCount = write;
}

FLASHMEM void rationalizeRecordedLane(MacroAutomationLane& lane) {
    snapLaneToTickGrid(lane);
    simplifyLaneByLinearError(lane);
}

}  // namespace

FLASHMEM float macroAutomationClamp01(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, 0.0f, 1.0f);
}

FLASHMEM float macroAutomationClampSigned(float value) {
    if (!std::isfinite(value)) return 0.0f;
    return std::clamp(value, -1.0f, 1.0f);
}

FLASHMEM bool macroCurvePlaybackStateValid(MacroCurvePlaybackState state) {
    switch (state) {
        case MacroCurvePlaybackState::ACTIVE:
        case MacroCurvePlaybackState::OFF:
        case MacroCurvePlaybackState::SUSPENDED_AFTER_RECORD:
            return true;
        default:
            return false;
    }
}

FLASHMEM bool macroModulationOriginValid(MacroModulationOrigin origin) {
    switch (origin) {
        case MacroModulationOrigin::NATIVE:
        case MacroModulationOrigin::CONVERTED_MEAN:
        case MacroModulationOrigin::CONVERTED_FIRST:
        case MacroModulationOrigin::CONVERTED_MIN:
            return true;
        default:
            return false;
    }
}

FLASHMEM bool macroAutomationCurveLifecycleValid(
    const MacroAutomationCurveRef& curve
) {
    return macroCurvePlaybackStateValid(curve.playbackState) &&
           curve.playbackState != MacroCurvePlaybackState::SUSPENDED_AFTER_RECORD &&
           curve.modulationOrigin == MacroModulationOrigin::NATIVE;
}

FLASHMEM bool macroModulationCurveLifecycleValid(
    const MacroAutomationCurveRef& curve
) {
    return macroCurvePlaybackStateValid(curve.playbackState) &&
           macroModulationOriginValid(curve.modulationOrigin);
}

FLASHMEM bool macroCurveStored(const MacroAutomationCurveRef& curve) {
    return curve.active && curve.pointCount > 0;
}

FLASHMEM bool macroCurvePlaybackActive(const MacroAutomationCurveRef& curve) {
    return macroCurveStored(curve) &&
           curve.playbackState == MacroCurvePlaybackState::ACTIVE;
}

FLASHMEM bool macroCurveSuspendedAfterRecord(const MacroAutomationCurveRef& curve) {
    return macroCurveStored(curve) &&
           curve.playbackState == MacroCurvePlaybackState::SUSPENDED_AFTER_RECORD;
}

FLASHMEM MacroModulationOrigin macroModulationOriginForConversion(
    MacroAutomationConversionPolicy policy
) {
    switch (policy) {
        case MacroAutomationConversionPolicy::FIRST:
            return MacroModulationOrigin::CONVERTED_FIRST;
        case MacroAutomationConversionPolicy::MIN:
            return MacroModulationOrigin::CONVERTED_MIN;
        case MacroAutomationConversionPolicy::MEAN:
        default:
            return MacroModulationOrigin::CONVERTED_MEAN;
    }
}

FLASHMEM float macroAutomationElapsedBeats(
    uint32_t startedAtMs,
    uint32_t nowMs,
    float tempoBpm
) {
    const int32_t elapsedMs = oc::time::signedDeltaMs(nowMs, startedAtMs);
    if (elapsedMs <= 0) return 0.0f;
    const float tempo = tempoBpm > 0.0f ? tempoBpm : 120.0f;
    return (static_cast<float>(elapsedMs) * tempo) / 60000.0f;
}

FLASHMEM float macroAutomationQuantizeDurationBeats(float rawDurationBeats) {
    const float raw = sanitizeDuration(rawDurationBeats);
    if (raw > kShortDurationTableBeats.back()) {
        const float bars = std::max(1.0f, std::round(raw / kBarBeats));
        return sanitizeDuration(bars * kBarBeats);
    }

    float best = kShortDurationTableBeats[0];
    float bestDistance = std::fabs(raw - best);
    for (float candidate : kShortDurationTableBeats) {
        const float distance = std::fabs(raw - candidate);
        if (distance <= bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

FLASHMEM float macroAutomationBeatsFromTicks(uint16_t ticks) {
    return static_cast<float>(ticks) /
           static_cast<float>(MACRO_AUTOMATION_TICKS_PER_BEAT);
}

FLASHMEM uint16_t macroAutomationTicksFromBeats(float beats) {
    const float sanitized = sanitizeDuration(beats);
    const float ticks =
        sanitized * static_cast<float>(MACRO_AUTOMATION_TICKS_PER_BEAT);
    return static_cast<uint16_t>(std::clamp(
        static_cast<int>(std::lround(ticks)),
        1,
        static_cast<int>(UINT16_MAX)
    ));
}

FLASHMEM int16_t macroAutomationPackValue(float value, bool signedInput) {
    const float clamped = signedInput ? macroAutomationClampSigned(value)
                                      : macroAutomationClamp01(value);
    return static_cast<int16_t>(std::lround(
        clamped * static_cast<float>(kPackedValueMax)
    ));
}

FLASHMEM float macroAutomationUnpackValue(int16_t packed, bool signedOutput) {
    const float normalized =
        static_cast<float>(packed) / static_cast<float>(kPackedValueMax);
    return signedOutput ? macroAutomationClampSigned(normalized)
                        : macroAutomationClamp01(normalized);
}

FLASHMEM bool macroAutomationAppendPoint(MacroAutomationLane& lane,
                                         float beat,
                                         float value,
                                         bool* reduced) {
    if (reduced != nullptr) *reduced = false;
    if (!std::isfinite(beat) || beat < 0.0f) return false;
    if (lane.pointCount > 0 && beat < lane.points[lane.pointCount - 1].beat) return false;

    if (lane.pointCount >= MACRO_AUTOMATION_RECORDING_MAX_POINTS) {
        const uint16_t before = lane.pointCount;
        simplifyLaneByLinearError(lane);
        if (lane.pointCount >= MACRO_AUTOMATION_RECORDING_MAX_POINTS) {
            decimateLaneForContinuedRecording(lane);
        }
        if (reduced != nullptr) *reduced = lane.pointCount < before;
    }
    if (lane.pointCount >= MACRO_AUTOMATION_RECORDING_MAX_POINTS) return false;

    const uint16_t index = lane.pointCount;
    lane.points[index] = MacroCurvePoint{beat, macroAutomationClamp01(value)};
    lane.pointCount = static_cast<uint16_t>(lane.pointCount + 1U);
    lane.active = true;
    return true;
}

FLASHMEM bool macroModulationAppendPoint(MacroModulationShape& shape, float beat, float value) {
    if (shape.pointCount >= MACRO_AUTOMATION_RECORDING_MAX_POINTS) return false;
    if (!std::isfinite(beat) || beat < 0.0f) return false;
    if (shape.pointCount > 0 && beat < shape.points[shape.pointCount - 1].beat) return false;
    const uint16_t index = shape.pointCount;
    shape.points[index] = MacroCurvePoint{beat, macroAutomationClampSigned(value)};
    shape.pointCount = static_cast<uint16_t>(shape.pointCount + 1U);
    shape.active = true;
    return true;
}

FLASHMEM void macroAutomationFinalizeRecording(MacroAutomationLane& lane, float rawDurationBeats) {
    if (lane.pointCount == 0) {
        lane.active = false;
        lane.durationBeats = 1.0f;
        return;
    }

    const float raw = sanitizeDuration(rawDurationBeats);
    const float quantized = macroAutomationQuantizeDurationBeats(raw);
    remapLaneToDuration(lane, raw, quantized);
    rationalizeRecordedLane(lane);
}

FLASHMEM void macroAutomationFinalizeRecordingWithDuration(MacroAutomationLane& lane,
                                                           float rawDurationBeats,
                                                           float targetDurationBeats) {
    if (lane.pointCount == 0) {
        lane.active = false;
        lane.durationBeats = sanitizeDuration(targetDurationBeats);
        return;
    }

    remapLaneToDuration(lane, rawDurationBeats, targetDurationBeats);
    rationalizeRecordedLane(lane);
}

FLASHMEM float macroAutomationEvaluate(const MacroAutomationLane& lane,
                                       float beat,
                                       float fallbackValue) {
    if (!lane.active) return macroAutomationClamp01(fallbackValue);
    return evaluatePoints(
        lane.points,
        lane.pointCount,
        lane.durationBeats,
        beat,
        macroAutomationClamp01(fallbackValue),
        false
    );
}

FLASHMEM float macroAutomationEvaluate(const MacroAutomationCurveRef& lane,
                                       const MacroAutomationPointPool& pool,
                                       float beat,
                                       float fallbackValue) {
    if (!lane.active) return macroAutomationClamp01(fallbackValue);
    return evaluatePoolPoints(
        lane,
        pool,
        beat,
        macroAutomationClamp01(fallbackValue),
        false
    );
}

FLASHMEM float macroModulationEvaluate(const MacroModulationShape& shape, float beat) {
    if (!shape.active) return 0.0f;
    return evaluatePoints(
        shape.points,
        shape.pointCount,
        shape.durationBeats,
        beat,
        0.0f,
        true
    );
}

FLASHMEM float macroModulationEvaluate(const MacroAutomationCurveRef& shape,
                                       const MacroAutomationPointPool& pool,
                                       float beat) {
    if (!shape.active) return 0.0f;
    return evaluatePoolPoints(shape, pool, beat, 0.0f, true);
}

FLASHMEM bool macroAutomationReadPoint(const MacroAutomationCurveRef& lane,
                                       const MacroAutomationPointPool& pool,
                                       uint16_t index,
                                       bool signedOutput,
                                       MacroCurvePoint& out) {
    if (!lane.active || index >= lane.pointCount) return false;
    const uint32_t poolIndex =
        static_cast<uint32_t>(lane.pointOffset) + static_cast<uint32_t>(index);
    if (poolIndex >= pool.used) return false;
    out = unpackPoolPoint(pool, static_cast<uint16_t>(poolIndex), signedOutput);
    return true;
}

FLASHMEM bool macroAutomationResizeCurveDuration(MacroAutomationCurveRef& lane,
                                                 MacroAutomationPointPool& pool,
                                                 float targetDurationBeats) {
    if (!lane.active || lane.pointCount == 0) return false;
    if (lane.pointOffset >= pool.used) return false;

    const uint16_t sourceTicks = sourceDurationTicks(lane, pool);
    const uint16_t targetTicks = macroAutomationTicksFromBeats(targetDurationBeats);
    const bool changed = lane.durationTicks != targetTicks ||
                         lane.sourceDurationTicks != sourceTicks;
    if (!changed) return false;

    lane.sourceDurationTicks = sourceTicks;
    lane.durationTicks = targetTicks;
    lane.windowOffsetTicks = wrapSourceTick(lane.windowOffsetTicks, sourceTicks);
    return true;
}

FLASHMEM bool macroAutomationSetCurveWindowOffset(MacroAutomationCurveRef& lane,
                                                  const MacroAutomationPointPool& pool,
                                                  float targetOffsetBeats) {
    if (!lane.active || lane.pointCount == 0) return false;
    if (lane.pointOffset >= pool.used) return false;

    const uint16_t sourceTicks = sourceDurationTicks(lane, pool);
    const uint16_t targetTicks = wrapSourceTick(
        offsetTicksFromBeats(targetOffsetBeats),
        sourceTicks
    );
    const bool changed = lane.windowOffsetTicks != targetTicks ||
                         lane.sourceDurationTicks != sourceTicks;
    if (!changed) return false;

    lane.sourceDurationTicks = sourceTicks;
    lane.windowOffsetTicks = targetTicks;
    return true;
}

FLASHMEM MacroAutomationCurveWindowSummary macroAutomationCurveWindowSummary(
    const MacroAutomationCurveRef& lane,
    const MacroAutomationPointPool& pool
) {
    MacroAutomationCurveWindowSummary summary{};
    if (!lane.active || lane.pointCount == 0 || lane.pointOffset >= pool.used) {
        return summary;
    }

    const uint16_t count = availablePoolPointCount(lane, pool);
    if (count == 0) {
        return summary;
    }

    const uint16_t sourceTicks = sourceDurationTicks(lane, pool);
    const uint16_t durationTicks = lane.durationTicks == 0U
        ? MACRO_AUTOMATION_TICKS_PER_BEAT
        : lane.durationTicks;
    const uint16_t offsetTicks = wrapSourceTick(lane.windowOffsetTicks, sourceTicks);
    const uint16_t firstTick = pool.points[lane.pointOffset].tick;
    const uint16_t lastTick = pool.points[
        static_cast<uint16_t>(lane.pointOffset + count - 1U)
    ].tick;
    const uint32_t windowEnd =
        static_cast<uint32_t>(offsetTicks) + static_cast<uint32_t>(durationTicks);

    summary.active = true;
    summary.sourceDurationTicks = sourceTicks;
    summary.durationTicks = durationTicks;
    summary.windowOffsetTicks = offsetTicks;
    summary.firstPointTick = std::min<uint16_t>(firstTick, sourceTicks);
    summary.lastPointTick = std::min<uint16_t>(lastTick, sourceTicks);
    summary.pointCount = count;
    summary.wraps = windowEnd > sourceTicks;
    return summary;
}

FLASHMEM bool macroAutomationConvertToModulation(
    const MacroAutomationLane& automation,
    MacroAutomationConversionPolicy policy,
    MacroModulationShape& outShape
) {
    outShape = MacroModulationShape{};
    if (!automation.active || automation.pointCount == 0) return false;

    const uint16_t count =
        std::min<uint16_t>(automation.pointCount, MACRO_AUTOMATION_RECORDING_MAX_POINTS);
    float reference = 0.0f;
    switch (policy) {
        case MacroAutomationConversionPolicy::FIRST:
            reference = macroAutomationClamp01(automation.points[0].value);
            break;
        case MacroAutomationConversionPolicy::MIN:
            reference = 1.0f;
            for (uint16_t i = 0; i < count; ++i) {
                reference = std::min(reference, macroAutomationClamp01(automation.points[i].value));
            }
            break;
        case MacroAutomationConversionPolicy::MEAN:
        default:
            for (uint16_t i = 0; i < count; ++i) {
                reference += macroAutomationClamp01(automation.points[i].value);
            }
            reference /= static_cast<float>(count);
            break;
    }

    outShape.durationBeats = sanitizeDuration(automation.durationBeats);
    outShape.interpolation = automation.interpolation;
    for (uint16_t i = 0; i < count; ++i) {
        const float relative = macroAutomationClamp01(automation.points[i].value) - reference;
        macroModulationAppendPoint(outShape, automation.points[i].beat, relative);
    }
    outShape.active = outShape.pointCount > 0;
    return outShape.active;
}

FLASHMEM MacroResolvedValue macroResolveValue(float staticValue,
                                              const MacroAutomationSlotState& slot,
                                              const MacroAutomationPointPool& pool,
                                              float beat) {
    MacroResolvedValue result{};
    result.automationStored = macroCurveStored(slot.automation);
    result.modulationStored = macroCurveStored(slot.modulation);
    result.automationActive = macroCurvePlaybackActive(slot.automation);
    result.modulationSuspended = macroCurveSuspendedAfterRecord(slot.modulation);
    result.modulationPausedDepthZero =
        macroCurvePlaybackActive(slot.modulation) && slot.modulationDepth <= 0.0f;
    result.modulationActive =
        macroCurvePlaybackActive(slot.modulation) && slot.modulationDepth > 0.0f;
    const float staticBase = macroAutomationClamp01(staticValue);
    result.base = result.automationActive
        ? macroAutomationEvaluate(slot.automation, pool, beat, staticBase)
        : staticBase;
    result.modulation = result.modulationActive
        ? macroModulationEvaluate(slot.modulation, pool, beat) *
              macroAutomationClamp01(slot.modulationDepth)
        : 0.0f;
    result.resolved = macroAutomationClamp01(result.base + result.modulation);
    return result;
}

}  // namespace core::state::macro

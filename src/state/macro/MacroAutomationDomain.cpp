#include "state/macro/MacroAutomationDomain.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>
#include <oc/time/Time.hpp>

namespace core::state::macro {

namespace {

constexpr std::array<float, 5> kShortDurationTableBeats PROGMEM{
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

FLASHMEM void collapseConstantLane(MacroAutomationLane& lane) {
    if (lane.pointCount <= 1U) return;
    const float first = lane.points[0].value;
    for (uint16_t i = 1; i < lane.pointCount; ++i) {
        if (std::fabs(lane.points[i].value - first) >
            kAutomationSimplifyMaxError) {
            return;
        }
    }
    lane.pointCount = 1;
    lane.active = true;
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
    collapseConstantLane(lane);
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

}  // namespace core::state::macro

#include "state/macro/MacroAutomationDomain.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

namespace {

constexpr std::array<float, 8> kDurationTableBeats{
    0.25f,
    0.5f,
    1.0f,
    2.0f,
    4.0f,
    8.0f,
    16.0f,
    32.0f,
};

FLASHMEM float sanitizeDuration(float durationBeats) {
    if (!std::isfinite(durationBeats) || durationBeats <= 0.0f) {
        return 1.0f;
    }
    return durationBeats;
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
                              uint8_t pointCount,
                              float durationBeats,
                              float beat,
                              float fallbackValue,
                              bool signedOutput) {
    if (pointCount == 0) return fallbackValue;

    const uint8_t count = std::min<uint8_t>(pointCount, MACRO_AUTOMATION_MAX_POINTS);
    if (count == 1) {
        return signedOutput ? macroAutomationClampSigned(points[0].value)
                            : macroAutomationClamp01(points[0].value);
    }

    const float duration = sanitizeDuration(durationBeats);
    const float wrapped = wrapBeat(beat, duration);

    const auto& first = points[0];
    if (wrapped <= first.beat && first.beat <= 0.000001f) {
        return signedOutput ? macroAutomationClampSigned(first.value)
                            : macroAutomationClamp01(first.value);
    }
    if (wrapped <= first.beat) {
        const auto& last = points[count - 1];
        const float previousBeat = last.beat - duration;
        const float value = interpolateLinear(
            previousBeat,
            last.value,
            first.beat,
            first.value,
            wrapped
        );
        return signedOutput ? macroAutomationClampSigned(value) : macroAutomationClamp01(value);
    }

    for (uint8_t i = 1; i < count; ++i) {
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
    const float value = interpolateLinear(
        last.beat,
        last.value,
        first.beat + duration,
        first.value,
        wrapped
    );
    return signedOutput ? macroAutomationClampSigned(value) : macroAutomationClamp01(value);
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

FLASHMEM float macroAutomationQuantizeDurationBeats(float rawDurationBeats) {
    const float raw = sanitizeDuration(rawDurationBeats);
    float best = kDurationTableBeats[0];
    float bestDistance = std::fabs(raw - best);
    for (float candidate : kDurationTableBeats) {
        const float distance = std::fabs(raw - candidate);
        if (distance <= bestDistance) {
            best = candidate;
            bestDistance = distance;
        }
    }
    return best;
}

FLASHMEM bool macroAutomationAppendPoint(MacroAutomationLane& lane, float beat, float value) {
    if (lane.pointCount >= MACRO_AUTOMATION_MAX_POINTS) return false;
    if (!std::isfinite(beat) || beat < 0.0f) return false;
    if (lane.pointCount > 0 && beat < lane.points[lane.pointCount - 1].beat) return false;
    const uint8_t index = lane.pointCount;
    lane.points[index] = MacroCurvePoint{beat, macroAutomationClamp01(value)};
    lane.pointCount = static_cast<uint8_t>(lane.pointCount + 1U);
    lane.active = true;
    return true;
}

FLASHMEM bool macroModulationAppendPoint(MacroModulationShape& shape, float beat, float value) {
    if (shape.pointCount >= MACRO_AUTOMATION_MAX_POINTS) return false;
    if (!std::isfinite(beat) || beat < 0.0f) return false;
    if (shape.pointCount > 0 && beat < shape.points[shape.pointCount - 1].beat) return false;
    const uint8_t index = shape.pointCount;
    shape.points[index] = MacroCurvePoint{beat, macroAutomationClampSigned(value)};
    shape.pointCount = static_cast<uint8_t>(shape.pointCount + 1U);
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
    const float scale = quantized / raw;
    const uint8_t count = std::min<uint8_t>(lane.pointCount, MACRO_AUTOMATION_MAX_POINTS);
    for (uint8_t i = 0; i < count; ++i) {
        lane.points[i].beat = std::clamp(lane.points[i].beat * scale, 0.0f, quantized);
        lane.points[i].value = macroAutomationClamp01(lane.points[i].value);
    }
    lane.durationBeats = quantized;
    lane.active = true;
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

FLASHMEM bool macroAutomationConvertToModulation(
    const MacroAutomationLane& automation,
    MacroAutomationConversionPolicy policy,
    MacroModulationShape& outShape
) {
    outShape = MacroModulationShape{};
    if (!automation.active || automation.pointCount == 0) return false;

    const uint8_t count = std::min<uint8_t>(automation.pointCount, MACRO_AUTOMATION_MAX_POINTS);
    float reference = 0.0f;
    switch (policy) {
        case MacroAutomationConversionPolicy::FIRST:
            reference = macroAutomationClamp01(automation.points[0].value);
            break;
        case MacroAutomationConversionPolicy::MIN:
            reference = 1.0f;
            for (uint8_t i = 0; i < count; ++i) {
                reference = std::min(reference, macroAutomationClamp01(automation.points[i].value));
            }
            break;
        case MacroAutomationConversionPolicy::MEAN:
        default:
            for (uint8_t i = 0; i < count; ++i) {
                reference += macroAutomationClamp01(automation.points[i].value);
            }
            reference /= static_cast<float>(count);
            break;
    }

    outShape.durationBeats = sanitizeDuration(automation.durationBeats);
    outShape.interpolation = automation.interpolation;
    for (uint8_t i = 0; i < count; ++i) {
        const float relative = macroAutomationClamp01(automation.points[i].value) - reference;
        macroModulationAppendPoint(outShape, automation.points[i].beat, relative);
    }
    outShape.active = outShape.pointCount > 0;
    return outShape.active;
}

FLASHMEM MacroResolvedValue macroResolveValue(float staticValue,
                                              const MacroAutomationSlotState& slot,
                                              float beat) {
    MacroResolvedValue result{};
    result.automationActive = slot.automation.active;
    result.modulationActive = slot.modulation.active && slot.modulationDepth > 0.0f;
    result.base = macroAutomationEvaluate(
        slot.automation,
        beat,
        macroAutomationClamp01(staticValue)
    );
    result.modulation = result.modulationActive
        ? macroModulationEvaluate(slot.modulation, beat) *
              macroAutomationClamp01(slot.modulationDepth)
        : 0.0f;
    result.resolved = macroAutomationClamp01(result.base + result.modulation);
    return result;
}

}  // namespace core::state::macro

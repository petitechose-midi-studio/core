#include "state/modulation/ProjectControlRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ModulatorEnvelopeTiming.hpp"
#include "state/modulation/ProjectControlRuntimeInternal.hpp"

namespace core::state::modulation {
namespace project_control_runtime_detail {
float wrapPhase(float phase) {
    if (!std::isfinite(phase)) return 0.0f;
    phase -= std::floor(phase);
    return phase < 0.0f ? phase + 1.0f : phase;
}

float phaseFromMusicalTime(
    const ProjectControlTimeSnapshot& time,
    uint32_t anchorTick,
    uint16_t anchorFractionQ16,
    uint32_t periodTicks,
    int16_t phaseQ15
) {
    if (periodTicks == 0U) return 0.0f;
    uint32_t elapsedTicks = time.musicalTick - anchorTick;
    int32_t fraction = static_cast<int32_t>(time.musicalTickFractionQ16) -
        static_cast<int32_t>(anchorFractionQ16);
    if (fraction < 0) {
        --elapsedTicks;
        fraction += static_cast<int32_t>(Q16_SCALE);
    }
    const uint32_t cycleTick = elapsedTicks % periodTicks;
    const float local = static_cast<float>(cycleTick) +
        static_cast<float>(fraction) / Q16_SCALE;
    const float authoredPhase = static_cast<float>(phaseQ15) / Q15_SCALE;
    return wrapPhase(local / static_cast<float>(periodTicks) + authoredPhase);
}

void elapsedMusicalTime(
    const ProjectControlTimeSnapshot& time,
    uint32_t anchorTick,
    uint16_t anchorFractionQ16,
    uint32_t& elapsedTick,
    uint16_t& elapsedFractionQ16
) {
    elapsedTick = time.musicalTick - anchorTick;
    int32_t fraction = static_cast<int32_t>(time.musicalTickFractionQ16) -
        static_cast<int32_t>(anchorFractionQ16);
    if (fraction < 0) {
        --elapsedTick;
        fraction += static_cast<int32_t>(Q16_SCALE);
    }
    elapsedFractionQ16 = static_cast<uint16_t>(fraction);
}

float phaseFromFreeTime(uint32_t nowMs,
                       uint32_t anchorMs,
                       uint32_t periodMs,
                       int16_t phaseQ15) {
    if (periodMs == 0U) return 0.0f;
    const uint32_t cycleMs = (nowMs - anchorMs) % periodMs;
    const float authoredPhase = static_cast<float>(phaseQ15) / Q15_SCALE;
    return wrapPhase(
        static_cast<float>(cycleMs) / static_cast<float>(periodMs) +
        authoredPhase
    );
}

float unpackProjectCurveValue(int16_t value,
                              ProjectCurveValueDomain domain) {
    const float unpacked = static_cast<float>(value) / Q15_SCALE;
    return domain == ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
        ? std::clamp(unpacked, 0.0f, 1.0f)
        : std::clamp(unpacked, -1.0f, 1.0f);
}

float interpolatePackedCurveSegment(
    float sourceTick,
    uint16_t leftTick,
    uint16_t rightTick,
    int16_t leftPackedValue,
    int16_t rightPackedValue,
    ProjectCurveValueDomain domain
) {
    const float rightValue = unpackProjectCurveValue(
        rightPackedValue,
        domain
    );
    const uint16_t span = static_cast<uint16_t>(rightTick - leftTick);
    if (span == 0U) return rightValue;
    const float leftValue = unpackProjectCurveValue(leftPackedValue, domain);
    const float alpha = std::clamp(
        (sourceTick - static_cast<float>(leftTick)) /
            static_cast<float>(span),
        0.0f,
        1.0f
    );
    const float value = leftValue + (rightValue - leftValue) * alpha;
    return domain == ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
        ? std::clamp(value, 0.0f, 1.0f)
        : std::clamp(value, -1.0f, 1.0f);
}

uint16_t curveRightPoint(
    const ProjectCurveArena& arena,
    const ProjectModulationRuntimeCurve& curve,
    float sourceTick,
    uint16_t* segmentHint
) {
    constexpr uint8_t MAX_LOCAL_HINT_STEPS = 4U;
    if (segmentHint != nullptr && *segmentHint > 0U &&
        *segmentHint < curve.pointCount) {
        uint16_t right = *segmentHint;
        uint8_t steps = 0U;
        if (arena.points[static_cast<uint16_t>(curve.pointOffset + right)].tick <
            sourceTick) {
            while (right + 1U < curve.pointCount &&
                   steps < MAX_LOCAL_HINT_STEPS) {
                ++right;
                ++steps;
                if (arena.points[static_cast<uint16_t>(
                        curve.pointOffset + right
                    )].tick >= sourceTick) {
                    *segmentHint = right;
                    return right;
                }
            }
        } else {
            while (right > 1U && steps < MAX_LOCAL_HINT_STEPS &&
                   arena.points[static_cast<uint16_t>(
                       curve.pointOffset + right - 1U
                   )].tick >= sourceTick) {
                --right;
                ++steps;
            }
            if (arena.points[static_cast<uint16_t>(
                    curve.pointOffset + right - 1U
                )].tick < sourceTick) {
                *segmentHint = right;
                return right;
            }
        }
    }

    uint16_t low = 1U;
    uint16_t high = curve.pointCount;
    while (low < high) {
        const uint16_t mid = static_cast<uint16_t>(
            low + (high - low) / 2U
        );
        if (arena.points[static_cast<uint16_t>(curve.pointOffset + mid)].tick <
            sourceTick) {
            low = static_cast<uint16_t>(mid + 1U);
        } else {
            high = mid;
        }
    }
    if (segmentHint != nullptr) *segmentHint = low;
    return low;
}

float evaluateProjectCurve(
    const ProjectCurveArena& arena,
    const ProjectModulationRuntimeCurve& curve,
    uint32_t elapsedTick,
    uint16_t elapsedFractionQ16,
    float fallback,
    ProjectModulationRuntimeRecordedCurveState* cache
) {
    if (curve.pointCount == 0U ||
        curve.pointOffset >= arena.pointCount ||
        static_cast<uint32_t>(curve.pointOffset) + curve.pointCount >
            arena.pointCount) {
        return fallback;
    }

    const uint16_t duration = std::max<uint16_t>(curve.durationTicks, 1U);
    const uint16_t sourceDuration = std::max<uint16_t>(
        curve.sourceDurationTicks,
        1U
    );
    const uint32_t localWhole = elapsedTick % duration;
    float sourceTick = static_cast<float>(
        (static_cast<uint32_t>(curve.windowOffsetTicks) + localWhole) %
        sourceDuration
    );
    sourceTick += static_cast<float>(elapsedFractionQ16) / Q16_SCALE;
    if (sourceTick >= static_cast<float>(sourceDuration)) {
        sourceTick -= static_cast<float>(sourceDuration);
    }

    if (cache != nullptr && cache->segmentValid &&
        cache->leftTick < sourceTick && sourceTick <= cache->rightTick) {
        return interpolatePackedCurveSegment(
            sourceTick,
            cache->leftTick,
            cache->rightTick,
            cache->leftValue,
            cache->rightValue,
            curve.valueDomain
        );
    }

    const uint16_t firstIndex = curve.pointOffset;
    const uint16_t lastIndex = static_cast<uint16_t>(
        curve.pointOffset + curve.pointCount - 1U
    );
    const auto& first = arena.points[firstIndex];
    if (curve.pointCount == 1U || sourceTick <= first.tick) {
        if (cache != nullptr) {
            cache->segmentValid = false;
            cache->segmentHint = 1U;
        }
        return unpackProjectCurveValue(first.value, curve.valueDomain);
    }
    const auto& last = arena.points[lastIndex];
    if (sourceTick >= last.tick) {
        if (cache != nullptr) {
            cache->segmentValid = false;
            cache->segmentHint = static_cast<uint16_t>(
                curve.pointCount - 1U
            );
        }
        return unpackProjectCurveValue(last.value, curve.valueDomain);
    }

    const uint16_t low = curveRightPoint(
        arena,
        curve,
        sourceTick,
        cache != nullptr ? &cache->segmentHint : nullptr
    );
    const uint16_t rightIndex = static_cast<uint16_t>(
        curve.pointOffset + low
    );
    const uint16_t leftIndex = static_cast<uint16_t>(rightIndex - 1U);
    const auto& left = arena.points[leftIndex];
    const auto& right = arena.points[rightIndex];
    if (cache != nullptr) {
        cache->segmentValid = true;
        cache->leftTick = left.tick;
        cache->rightTick = right.tick;
        cache->leftValue = left.value;
        cache->rightValue = right.value;
    }
    return interpolatePackedCurveSegment(
        sourceTick,
        left.tick,
        right.tick,
        left.value,
        right.value,
        curve.valueDomain
    );
}

float evaluateProjectCurve(const ProjectCurveArena& arena,
                           uint16_t recordIndex,
                           uint32_t elapsedTick,
                           uint16_t elapsedFractionQ16,
                           float fallback) {
    if (recordIndex == INVALID_CURVE_RECORD ||
        recordIndex >= arena.recordCount) {
        return fallback;
    }
    const auto& record = arena.records[recordIndex];
    const ProjectModulationRuntimeCurve curve{
        .pointOffset = record.pointOffset,
        .pointCount = record.pointCount,
        .sourceDurationTicks = record.sourceDurationTicks,
        .durationTicks = record.durationTicks,
        .windowOffsetTicks = record.windowOffsetTicks,
        .valueDomain = record.valueDomain,
        .reserved = 0U,
    };
    return evaluateProjectCurve(
        arena,
        curve,
        elapsedTick,
        elapsedFractionQ16,
        fallback,
        nullptr
    );
}

}  // namespace project_control_runtime_detail

using namespace project_control_runtime_detail;

FLASHMEM void publishProjectControlTimeTelemetry(
    ProjectControlTimeTelemetry& telemetry,
    const ProjectControlTimeSnapshot& time
) {
    if (telemetry.revision == 0U) {
        telemetry.previous = time;
    } else {
        telemetry.previous = telemetry.current;
    }
    telemetry.current = time;
    ++telemetry.revision;
    if (telemetry.revision == 0U) telemetry.revision = 1U;
}

FLASHMEM ProjectControlTimeSnapshot extrapolateProjectControlTime(
    const ProjectControlTimeTelemetry& telemetry,
    uint32_t nowMs
) {
    ProjectControlTimeSnapshot result = telemetry.current;
    result.monotonicMs = nowMs;
    if (telemetry.revision < 2U || !telemetry.current.playing ||
        telemetry.previous.transportGeneration !=
            telemetry.current.transportGeneration ||
        telemetry.current.monotonicMs <= telemetry.previous.monotonicMs ||
        telemetry.current.musicalTick < telemetry.previous.musicalTick ||
        static_cast<int32_t>(nowMs - telemetry.current.monotonicMs) <= 0) {
        return result;
    }

    const uint32_t observedMs = telemetry.current.monotonicMs -
        telemetry.previous.monotonicMs;
    const int64_t observedQ16 =
        (static_cast<int64_t>(telemetry.current.musicalTick) -
         static_cast<int64_t>(telemetry.previous.musicalTick)) * 65536LL +
        static_cast<int32_t>(telemetry.current.musicalTickFractionQ16) -
        static_cast<int32_t>(telemetry.previous.musicalTickFractionQ16);
    if (observedQ16 <= 0 || observedMs == 0U) return result;

    // A hidden/stalled UI must never extrapolate an unbounded fictional
    // future. Normal LVGL service is well below this guard.
    constexpr uint32_t MAX_EXTRAPOLATION_MS = 100U;
    const uint32_t aheadMs = std::min<uint32_t>(
        nowMs - telemetry.current.monotonicMs,
        MAX_EXTRAPOLATION_MS
    );
    const uint64_t currentQ16 =
        (static_cast<uint64_t>(telemetry.current.musicalTick) << 16U) |
        telemetry.current.musicalTickFractionQ16;
    const uint64_t advancedQ16 = currentQ16 + static_cast<uint64_t>(
        (observedQ16 * static_cast<int64_t>(aheadMs)) /
        static_cast<int64_t>(observedMs)
    );
    result.musicalTick = static_cast<uint32_t>(advancedQ16 >> 16U);
    result.musicalTickFractionQ16 = static_cast<uint16_t>(advancedQ16);
    return result;
}

FLASHMEM uint16_t projectControlTimelinePositionQ16(
    const ProjectControlRuntimeState& state,
    const ProjectControlTimeSnapshot& time,
    uint16_t durationTicks
) {
    if (!state.initialized || durationTicks == 0U) return 0U;
    uint32_t elapsedTick = 0U;
    uint16_t elapsedFraction = 0U;
    elapsedMusicalTime(
        time,
        state.activationMusicalTick,
        state.activationMusicalTickFractionQ16,
        elapsedTick,
        elapsedFraction
    );
    const uint64_t localQ16 =
        (static_cast<uint64_t>(elapsedTick % durationTicks) << 16U) +
        elapsedFraction;
    return static_cast<uint16_t>(std::min<uint64_t>(
        65535U,
        (localQ16 * 65535ULL) /
            (static_cast<uint64_t>(durationTicks) << 16U)
    ));
}

namespace {
FLASHMEM int32_t lfoAuthoredPhaseQ16(int16_t authoredPhaseQ15) {
    const int32_t signedPhase = authoredPhaseQ15;
    const int64_t magnitude = signedPhase < 0
        ? -static_cast<int64_t>(signedPhase)
        : static_cast<int64_t>(signedPhase);
    const int32_t scaled = static_cast<int32_t>(
        (magnitude * 65535LL + 16383LL) / 32767LL
    );
    return signedPhase < 0 ? -scaled : scaled;
}

FLASHMEM uint16_t wrapPositionQ16(int32_t position) {
    position %= 65536;
    if (position < 0) position += 65536;
    return static_cast<uint16_t>(position);
}
}  // namespace

FLASHMEM uint16_t projectLfoPreviewPositionQ16(
    uint16_t runtimePositionQ16,
    int16_t authoredPhaseQ15
) {
    return wrapPositionQ16(
        static_cast<int32_t>(runtimePositionQ16) -
        lfoAuthoredPhaseQ16(authoredPhaseQ15)
    );
}

FLASHMEM uint16_t projectLfoShapePositionQ16(
    uint16_t previewPositionQ16,
    int16_t authoredPhaseQ15
) {
    return wrapPositionQ16(
        static_cast<int32_t>(previewPositionQ16) +
        lfoAuthoredPhaseQ16(authoredPhaseQ15)
    );
}

FLASHMEM bool projectModulatorRuntimeProjectionAtIndex(
    const ProjectModulationRuntimePlan& plan,
    const ProjectCurveArena& arena,
    const ProjectControlRuntimeState& state,
    const ProjectControlTimeSnapshot& time,
    uint16_t index,
    ProjectModulatorRuntimeProjection& out
) {
    out = {};
    if (!state.initialized || !validTime(time)) return false;
    if (index >= plan.sourceCount || index >= state.sourceCount ||
        state.sources[index].id != plan.sources[index].id) {
        return false;
    }
    const auto& source = plan.sources[index];
    const auto& runtime = state.sources[index];
    out.kind = source.kind;
    out.positionKnown = true;

    if (source.kind == ModulatorKind::ADSR) {
        auto copy = runtime.payload.adsr;
        out.rawValue = advanceAdsrRawToTime(source, copy, time);
        out.value = unpackQ15(advanceAdsrSmoothQ15(
            source,
            copy,
            time,
            state.lastEvaluationMs,
            state.lastEvaluationMusicalTick,
            state.lastEvaluationMusicalTickFractionQ16,
            out.rawValue
        ));
        out.adsrStage = copy.stage;
        float progress = 0.0f;
        if (copy.stage == ProjectModulationAdsrStage::DELAY ||
            copy.stage == ProjectModulationAdsrStage::ATTACK ||
            copy.stage == ProjectModulationAdsrStage::HOLD ||
            copy.stage == ProjectModulationAdsrStage::DECAY ||
            copy.stage == ProjectModulationAdsrStage::RELEASE) {
            (void)adsrStageCompleteAndProgress(
                copy,
                time,
                source.traits.adsr.timing,
                adsrStageDuration(source, copy.stage),
                progress
            );
        } else if (copy.stage == ProjectModulationAdsrStage::SUSTAIN) {
            progress = 0.5f;
        } else {
            out.positionKnown = false;
        }
        out.stageProgressQ16 = static_cast<uint16_t>(std::lround(
            std::clamp(progress, 0.0f, 1.0f) * 65535.0f
        ));
        return true;
    }

    uint32_t elapsedTick = 0U;
    uint16_t elapsedFraction = 0U;
    elapsedMusicalTime(
        time,
        state.activationMusicalTick,
        state.activationMusicalTickFractionQ16,
        elapsedTick,
        elapsedFraction
    );
    if (source.kind == ModulatorKind::RECORDED_SHAPE) {
        const uint16_t duration = std::max<uint16_t>(
            source.parameters.curve.durationTicks,
            1U
        );
        const uint64_t localQ16 =
            (static_cast<uint64_t>(elapsedTick % duration) << 16U) +
            elapsedFraction;
        out.positionQ16 = static_cast<uint16_t>(std::min<uint64_t>(
            65535U,
            (localQ16 * 65535ULL) /
                (static_cast<uint64_t>(duration) << 16U)
        ));
        out.value = evaluateProjectCurve(
            arena,
            source.parameters.curve,
            elapsedTick,
            elapsedFraction,
            0.0f,
            nullptr
        );
        out.rawValue = out.value;
        return true;
    }

    uint32_t musicalAnchor = state.activationMusicalTick;
    uint16_t musicalAnchorFraction = state.activationMusicalTickFractionQ16;
    uint32_t monotonicAnchor = state.activationMonotonicMs;
    if (source.traits.lfo.retrigger == ModulatorRetriggerPolicy::TRANSPORT) {
        musicalAnchor = time.transportStartMusicalTick;
        musicalAnchorFraction = 0U;
        monotonicAnchor = time.transportStartMonotonicMs;
    } else if (
        source.traits.lfo.retrigger ==
            ModulatorRetriggerPolicy::EXPLICIT_TRIGGER &&
        runtime.payload.lfo.explicitlyTriggered
    ) {
        musicalAnchor = runtime.payload.lfo.explicitMusicalAnchorTick;
        musicalAnchorFraction =
            runtime.payload.lfo.explicitMusicalAnchorFractionQ16;
        monotonicAnchor = runtime.payload.lfo.explicitMonotonicAnchorMs;
    }
    const float phase = source.traits.lfo.timing == ModulatorTimingMode::FREE
        ? phaseFromFreeTime(
            time.monotonicMs,
            monotonicAnchor,
            source.parameters.lfo.freePeriodMs,
            source.parameters.lfo.phaseQ15
        )
        : phaseFromMusicalTime(
            time,
            musicalAnchor,
            musicalAnchorFraction,
            source.parameters.lfo.periodTicks,
            source.parameters.lfo.phaseQ15
        );
    out.positionQ16 = static_cast<uint16_t>(std::lround(
        std::clamp(phase, 0.0f, 1.0f) * 65535.0f
    ));
    out.value = evaluateProjectLfoShape(source.traits.lfo.shape, phase);
    out.rawValue = out.value;
    return true;
}

FLASHMEM bool projectModulatorRuntimeProjection(
    const ProjectModulationRuntimePlan& plan,
    const ProjectCurveArena& arena,
    const ProjectControlRuntimeState& state,
    const ProjectControlTimeSnapshot& time,
    ModulatorId sourceId,
    ProjectModulatorRuntimeProjection& out
) {
    uint16_t index = 0U;
    while (index < plan.sourceCount && plan.sources[index].id != sourceId) {
        ++index;
    }
    return projectModulatorRuntimeProjectionAtIndex(
        plan,
        arena,
        state,
        time,
        index,
        out
    );
}

}  // namespace core::state::modulation

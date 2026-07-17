#include "state/modulation/ProjectControlRuntime.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

#include <config/PlatformCompat.hpp>

namespace core::state::modulation {

namespace {

constexpr float Q15_SCALE = 32767.0f;
constexpr float Q16_SCALE = 65536.0f;
constexpr float ADSR_SUSTAIN_SCALE = 32768.0f;
constexpr uint8_t ADSR_RUNTIME_FLAG_SYNC = 0x01U;
constexpr uint16_t INVALID_CURVE_RECORD =
    std::numeric_limits<uint16_t>::max();

bool validTime(const ProjectControlTimeSnapshot& time) {
    return time.reserved == 0U;
}

bool validPlanBounds(const ProjectModulationRuntimePlan& plan) {
    return plan.sourceCount <= PROJECT_MODULATOR_CAPACITY &&
           plan.bindingCount <= PROJECT_MODULATION_BINDING_CAPACITY &&
           plan.destinationCount <=
               PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY &&
           plan.triggerRouteCount <= plan.sourceCount;
}

bool triggerMatches(const ModulationTriggerRef& configured,
                     const ModulationTriggerRef& incoming) {
    if (configured.kind != incoming.kind ||
        configured.track != incoming.track) {
        return false;
    }
    if (configured.kind != ModulationTriggerKind::TRACK_NOTE) {
        return configured.channel == incoming.channel &&
               configured.data == incoming.data;
    }
    const bool channelMatches =
        configured.channel == PROJECT_MODULATION_TRIGGER_ANY_CHANNEL ||
        configured.channel == incoming.channel;
    const bool noteMatches =
        configured.data == PROJECT_MODULATION_TRIGGER_ANY_NOTE ||
        configured.data == incoming.data;
    return channelMatches && noteMatches;
}

FLASHMEM bool idNeededAfter(const ProjectModulationRuntimePlan& plan,
                            uint16_t first,
                            ModulatorId id) {
    if (!valid(id)) return false;
    for (uint16_t index = first; index < plan.sourceCount; ++index) {
        if (plan.sources[index].id == id) return true;
    }
    return false;
}

FLASHMEM bool bindingIdNeededAfter(const ProjectModulationRuntimePlan& plan,
                                   uint16_t first,
                                   ModulationBindingId id) {
    if (!valid(id)) return false;
    for (uint16_t index = first; index < plan.bindingCount; ++index) {
        if (plan.bindings[index].id == id) return true;
    }
    return false;
}

FLASHMEM ProjectModulationRuntimeSourceState makeSourceState(
    ModulatorId id,
    const ProjectModulationRuntimeSource& source,
    const ProjectControlTimeSnapshot& time
) {
    ProjectModulationRuntimeSourceState state{};
    state.id = id;
    const ModulatorKind kind = source.kind;
    if (kind == ModulatorKind::RECORDED_SHAPE) {
        state.payload.recordedCurve = {};
        state.payload.recordedCurve.kind = kind;
        state.payload.recordedCurve.segmentHint = 1U;
    } else if (kind == ModulatorKind::ADSR) {
        state.payload.adsr = {};
        state.payload.adsr.kind = kind;
        state.payload.adsr.flags = source.traits.adsr.timing ==
                ModulatorTimingMode::SYNC
            ? ADSR_RUNTIME_FLAG_SYNC
            : 0U;
    } else {
        state.payload.lfo = {};
        state.payload.lfo.kind = kind;
        state.payload.lfo.explicitMusicalAnchorTick = time.musicalTick;
        state.payload.lfo.explicitMonotonicAnchorMs = time.monotonicMs;
        state.payload.lfo.explicitMusicalAnchorFractionQ16 =
            time.musicalTickFractionQ16;
    }
    return state;
}

FLASHMEM ModulatorKind sourceStateKind(
    const ProjectModulationRuntimeSourceState& state
) {
    // Both standard-layout union members share ModulatorKind as their common
    // initial sequence, so the discriminator is readable through either arm.
    return state.payload.lfo.kind;
}

FLASHMEM void preparePublishedSourceState(
    ProjectModulationRuntimeSourceState& state,
    const ProjectModulationRuntimeSource& source,
    const ProjectControlTimeSnapshot& time
) {
    if (sourceStateKind(state) != source.kind) {
        state = makeSourceState(source.id, source, time);
        return;
    }
    if (source.kind == ModulatorKind::RECORDED_SHAPE) {
        // The source ID can survive a curve edit. Never retain endpoints from
        // the previously published immutable plan/arena pair.
        state.payload.recordedCurve.segmentValid = false;
        state.payload.recordedCurve.segmentHint = 1U;
    } else if (source.kind == ModulatorKind::ADSR) {
        const uint8_t timingFlag = source.traits.adsr.timing ==
                ModulatorTimingMode::SYNC
            ? ADSR_RUNTIME_FLAG_SYNC
            : 0U;
        if ((state.payload.adsr.flags & ADSR_RUNTIME_FLAG_SYNC) != timingFlag) {
            state = makeSourceState(source.id, source, time);
        }
    }
}

FLASHMEM void synchronizeSources(ProjectControlRuntimeState& state,
                                 const ProjectModulationRuntimePlan& plan,
                                 const ProjectControlTimeSnapshot& time) {
    const uint16_t previousCount = state.sourceCount;
    const uint16_t searchCount = std::max(previousCount, plan.sourceCount);
    for (uint16_t index = previousCount;
         index < searchCount;
         ++index) {
        state.sources[index] = {};
    }

    for (uint16_t targetIndex = 0;
         targetIndex < plan.sourceCount;
         ++targetIndex) {
        const ModulatorId targetId = plan.sources[targetIndex].id;
        if (state.sources[targetIndex].id == targetId) {
            preparePublishedSourceState(
                state.sources[targetIndex],
                plan.sources[targetIndex],
                time
            );
            continue;
        }

        uint16_t found = searchCount;
        for (uint16_t candidate = static_cast<uint16_t>(targetIndex + 1U);
             candidate < searchCount;
             ++candidate) {
            if (state.sources[candidate].id == targetId) {
                found = candidate;
                break;
            }
        }
        if (found < searchCount) {
            std::swap(state.sources[targetIndex], state.sources[found]);
            preparePublishedSourceState(
                state.sources[targetIndex],
                plan.sources[targetIndex],
                time
            );
            continue;
        }

        const bool displacedNeeded = idNeededAfter(
            plan,
            static_cast<uint16_t>(targetIndex + 1U),
            state.sources[targetIndex].id
        );
        if (displacedNeeded) {
            for (uint16_t candidate = static_cast<uint16_t>(targetIndex + 1U);
                 candidate < searchCount;
                 ++candidate) {
                if (!idNeededAfter(
                        plan,
                        static_cast<uint16_t>(targetIndex + 1U),
                        state.sources[candidate].id
                    )) {
                    state.sources[candidate] = state.sources[targetIndex];
                    break;
                }
            }
        }
        state.sources[targetIndex] = makeSourceState(
            targetId,
            plan.sources[targetIndex],
            time
        );
    }
    for (uint16_t index = plan.sourceCount; index < previousCount; ++index) {
        state.sources[index] = {};
    }
    state.sourceCount = plan.sourceCount;
}

FLASHMEM void synchronizeBindings(ProjectControlRuntimeState& state,
                                  const ProjectModulationRuntimePlan& plan) {
    const uint16_t previousCount = state.bindingCount;
    const uint16_t searchCount = std::max(previousCount, plan.bindingCount);
    for (uint16_t index = previousCount;
         index < searchCount;
         ++index) {
        state.bindingIds[index] = {};
        state.bindingContributionQ15[index] = 0;
    }

    for (uint16_t targetIndex = 0;
         targetIndex < plan.bindingCount;
         ++targetIndex) {
        const ModulationBindingId targetId = plan.bindings[targetIndex].id;
        if (state.bindingIds[targetIndex] == targetId) continue;

        uint16_t found = searchCount;
        for (uint16_t candidate = static_cast<uint16_t>(targetIndex + 1U);
             candidate < searchCount;
             ++candidate) {
            if (state.bindingIds[candidate] == targetId) {
                found = candidate;
                break;
            }
        }
        if (found < searchCount) {
            std::swap(state.bindingIds[targetIndex], state.bindingIds[found]);
            std::swap(
                state.bindingContributionQ15[targetIndex],
                state.bindingContributionQ15[found]
            );
            continue;
        }

        const bool displacedNeeded = bindingIdNeededAfter(
            plan,
            static_cast<uint16_t>(targetIndex + 1U),
            state.bindingIds[targetIndex]
        );
        if (displacedNeeded) {
            for (uint16_t candidate = static_cast<uint16_t>(targetIndex + 1U);
                 candidate < searchCount;
                 ++candidate) {
                if (!bindingIdNeededAfter(
                        plan,
                        static_cast<uint16_t>(targetIndex + 1U),
                        state.bindingIds[candidate]
                    )) {
                    state.bindingIds[candidate] = state.bindingIds[targetIndex];
                    state.bindingContributionQ15[candidate] =
                        state.bindingContributionQ15[targetIndex];
                    break;
                }
            }
        }
        state.bindingIds[targetIndex] = targetId;
        state.bindingContributionQ15[targetIndex] = 0;
    }
    for (uint16_t index = plan.bindingCount; index < previousCount; ++index) {
        state.bindingIds[index] = {};
        state.bindingContributionQ15[index] = 0;
    }
    state.bindingCount = plan.bindingCount;
}

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

int16_t packQ15(float value) {
    const float scaled = std::clamp(value, -1.0f, 1.0f) * Q15_SCALE;
    // The Cortex-M hot path does not need a libm lround call for an already
    // bounded value. Truncation after a signed half-unit offset retains the
    // same round-half-away-from-zero contract over the complete Q15 range.
    return static_cast<int16_t>(
        scaled >= 0.0f ? scaled + 0.5f : scaled - 0.5f
    );
}

float unpackQ15(int16_t value) {
    return std::clamp(static_cast<float>(value) / Q15_SCALE, -1.0f, 1.0f);
}

float adsrSustainLevel(const ProjectModulationRuntimeSource& source) {
    return std::clamp(
        static_cast<float>(source.parameters.adsr.sustainQ15) /
            ADSR_SUSTAIN_SCALE,
        0.0f,
        1.0f
    );
}

void anchorAdsrStage(
    ProjectModulationRuntimeAdsrState& state,
    ProjectModulationAdsrStage stage,
    float startLevel,
    const ProjectControlTimeSnapshot& time,
    ModulatorTimingMode timing
) {
    state.stage = stage;
    state.stageStartLevelQ15 = packQ15(std::clamp(startLevel, 0.0f, 1.0f));
    if (timing == ModulatorTimingMode::SYNC) {
        state.stageAnchor = time.musicalTick;
        state.stageAnchorFractionQ16 = time.musicalTickFractionQ16;
    } else {
        state.stageAnchor = time.monotonicMs;
        state.stageAnchorFractionQ16 = 0U;
    }
}

bool adsrStageCompleteAndProgress(
    const ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time,
    ModulatorTimingMode timing,
    uint16_t duration,
    float& progress
) {
    if (timing == ModulatorTimingMode::FREE) {
        const uint32_t elapsed = time.monotonicMs - state.stageAnchor;
        if (elapsed >= duration) return true;
        progress = static_cast<float>(elapsed) /
            static_cast<float>(duration);
        return false;
    }
    uint32_t ticks = 0U;
    uint16_t fraction = 0U;
    elapsedMusicalTime(
        time,
        state.stageAnchor,
        state.stageAnchorFractionQ16,
        ticks,
        fraction
    );
    if (ticks >= duration) return true;
    progress = (
        static_cast<float>(ticks) +
        static_cast<float>(fraction) / Q16_SCALE
    ) / static_cast<float>(duration);
    return false;
}

void advanceAdsrAnchor(
    ProjectModulationRuntimeAdsrState& state,
    uint16_t duration
) {
    state.stageAnchor += duration;
}

uint16_t adsrStageDuration(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationAdsrStage stage
) {
    switch (stage) {
        case ProjectModulationAdsrStage::ATTACK:
            return source.parameters.adsr.attack;
        case ProjectModulationAdsrStage::DECAY:
            return source.parameters.adsr.decay;
        case ProjectModulationAdsrStage::RELEASE:
            return source.parameters.adsr.release;
        case ProjectModulationAdsrStage::IDLE:
        case ProjectModulationAdsrStage::SUSTAIN:
        default:
            return 0U;
    }
}

float adsrStageTarget(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationAdsrStage stage
) {
    switch (stage) {
        case ProjectModulationAdsrStage::ATTACK:
            return 1.0f;
        case ProjectModulationAdsrStage::DECAY:
            return adsrSustainLevel(source);
        case ProjectModulationAdsrStage::RELEASE:
        case ProjectModulationAdsrStage::IDLE:
            return 0.0f;
        case ProjectModulationAdsrStage::SUSTAIN:
        default:
            return adsrSustainLevel(source);
    }
}

float advanceAdsrToTime(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time
) {
    constexpr uint8_t MAX_IMMEDIATE_TRANSITIONS = 4U;
    const auto timing = source.traits.adsr.timing;
    for (uint8_t transition = 0U;
         transition < MAX_IMMEDIATE_TRANSITIONS;
         ++transition) {
        if (state.stage == ProjectModulationAdsrStage::IDLE) return 0.0f;
        if (state.stage == ProjectModulationAdsrStage::SUSTAIN) {
            return adsrSustainLevel(source);
        }

        const auto stage = state.stage;
        const uint16_t duration = adsrStageDuration(source, stage);
        const float target = adsrStageTarget(source, stage);
        float progress = 0.0f;
        if (duration == 0U || adsrStageCompleteAndProgress(
                state,
                time,
                timing,
                duration,
                progress
            )) {
            state.stageStartLevelQ15 = packQ15(target);
            advanceAdsrAnchor(state, duration);
            if (stage == ProjectModulationAdsrStage::ATTACK) {
                state.stage = ProjectModulationAdsrStage::DECAY;
            } else if (stage == ProjectModulationAdsrStage::DECAY) {
                state.stage = ProjectModulationAdsrStage::SUSTAIN;
            } else {
                state.stage = ProjectModulationAdsrStage::IDLE;
            }
            continue;
        }

        const float start = std::clamp(
            unpackQ15(state.stageStartLevelQ15),
            0.0f,
            1.0f
        );
        const float shaped = evaluateProjectAdsrProgress(
            source.traits.adsr.curve,
            progress
        );
        return std::clamp(start + (target - start) * shaped, 0.0f, 1.0f);
    }
    return adsrStageTarget(source, state.stage);
}

void applyProjectAdsrTriggerEvent(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time,
    const ProjectModulationTriggerEvent& event,
    float& value
) {
    if (event.edge == ProjectModulationTriggerEdge::GATE_ON) {
        const bool gateWasClosed = state.heldNoteCount == 0U;
        if (state.heldNoteCount < UINT8_MAX) ++state.heldNoteCount;
        if (source.traits.adsr.retrigger ==
                ModulatorAdsrRetriggerMode::RETRIGGER ||
            gateWasClosed) {
            anchorAdsrStage(
                state,
                ProjectModulationAdsrStage::ATTACK,
                value,
                time,
                source.traits.adsr.timing
            );
            if (source.parameters.adsr.attack == 0U) {
                value = advanceAdsrToTime(source, state, time);
            }
        }
        return;
    }

    if (event.edge != ProjectModulationTriggerEdge::GATE_OFF ||
        state.heldNoteCount == 0U) {
        return;
    }
    --state.heldNoteCount;
    if (state.heldNoteCount == 0U) {
        anchorAdsrStage(
            state,
            ProjectModulationAdsrStage::RELEASE,
            value,
            time,
            source.traits.adsr.timing
        );
        if (source.parameters.adsr.release == 0U) {
            value = advanceAdsrToTime(source, state, time);
        }
    }
}

bool routeProjectTriggerFrame(
    const ProjectModulationRuntimePlan& plan,
    const ProjectControlTimeSnapshot& time,
    const ProjectModulationTriggerFrame* triggers,
    ProjectControlRuntimeState& state,
    float* sourceValues
) {
    if (triggers == nullptr || triggers->count == 0U) return true;
    for (uint16_t eventIndex = 0U;
         eventIndex < triggers->count;
         ++eventIndex) {
        const auto& event = triggers->events[eventIndex];
        if (event.trigger.track >= PROJECT_MODULATION_TRACK_COUNT ||
            (event.trigger.channel >= 16U &&
             event.trigger.channel !=
                PROJECT_MODULATION_TRIGGER_ANY_CHANNEL) ||
            static_cast<uint8_t>(event.trigger.kind) >=
                PROJECT_MODULATION_TRIGGER_KIND_COUNT) {
            continue;
        }
        std::array<uint16_t, 2> buckets{{
            projectModulationTriggerBucketIndex(event.trigger),
            0U,
        }};
        uint8_t bucketCount = 1U;
        if (event.trigger.kind == ModulationTriggerKind::TRACK_NOTE &&
            event.trigger.channel != PROJECT_MODULATION_TRIGGER_ANY_CHANNEL &&
            (plan.triggerWildcardTrackMask &
             (1U << event.trigger.track)) != 0U) {
            auto wildcard = event.trigger;
            wildcard.channel = PROJECT_MODULATION_TRIGGER_ANY_CHANNEL;
            buckets[bucketCount++] = projectModulationTriggerBucketIndex(
                wildcard
            );
        }
        for (uint8_t bucketIndex = 0U;
             bucketIndex < bucketCount;
             ++bucketIndex) {
            const uint16_t bucket = buckets[bucketIndex];
            const uint16_t start = plan.triggerBucketStart[bucket];
            const uint16_t end = static_cast<uint16_t>(
                start + plan.triggerBucketCount[bucket]
            );
            if (end > plan.triggerRouteCount) return false;
            for (uint16_t route = start; route < end; ++route) {
                const uint16_t sourceIndex = plan.triggerSourceOrder[route];
                if (sourceIndex >= plan.sourceCount) return false;
                const auto& source = plan.sources[sourceIndex];
                if (!triggerMatches(source.trigger, event.trigger)) continue;
                auto& sourceState = state.sources[sourceIndex];
                if (source.kind == ModulatorKind::ADSR) {
                    applyProjectAdsrTriggerEvent(
                        source,
                        sourceState.payload.adsr,
                        time,
                        event,
                        sourceValues[sourceIndex]
                    );
                } else if (
                    source.kind == ModulatorKind::LFO &&
                    source.traits.lfo.retrigger ==
                        ModulatorRetriggerPolicy::EXPLICIT_TRIGGER &&
                    event.edge != ProjectModulationTriggerEdge::GATE_OFF
                ) {
                    sourceState.payload.lfo.explicitMusicalAnchorTick =
                        time.musicalTick;
                    sourceState.payload.lfo.explicitMonotonicAnchorMs =
                        time.monotonicMs;
                    sourceState.payload.lfo.explicitMusicalAnchorFractionQ16 =
                        time.musicalTickFractionQ16;
                    sourceState.payload.lfo.explicitlyTriggered = true;
                }
            }
        }
    }
    return true;
}

float applySlew(float previous,
                float target,
                uint16_t slewMs,
                uint32_t elapsedMs) {
    if (slewMs == 0U) return target;
    const float maximumDelta = std::min(
        1.0f,
        static_cast<float>(elapsedMs) / static_cast<float>(slewMs)
    );
    return previous + std::clamp(
        target - previous,
        -maximumDelta,
        maximumDelta
    );
}

}  // namespace

FLASHMEM void resetProjectControlRuntimeState(
    ProjectControlRuntimeState& state,
    const ProjectControlTimeSnapshot& time
) {
    state = {};
    state.activationMusicalTick = time.musicalTick;
    state.activationMonotonicMs = time.monotonicMs;
    state.lastEvaluationMs = time.monotonicMs;
    state.activationMusicalTickFractionQ16 =
        time.musicalTickFractionQ16;
    state.initialized = validTime(time);
}

FLASHMEM ProjectControlRuntimeStatus synchronizeProjectControlRuntimeState(
    ProjectControlRuntimeState& state,
    const ProjectModulationRuntimePlan& plan,
    const ProjectControlTimeSnapshot& time
) {
    if (!validPlanBounds(plan)) {
        return ProjectControlRuntimeStatus::INVALID_PLAN;
    }
    if (!validTime(time)) {
        return ProjectControlRuntimeStatus::INVALID_TIME;
    }
    if (!state.initialized) resetProjectControlRuntimeState(state, time);
    synchronizeSources(state, plan, time);
    synchronizeBindings(state, plan);
    return ProjectControlRuntimeStatus::OK;
}

float evaluateProjectLfoShape(ModulatorLfoShape shape, float phase) {
    const float normalized = wrapPhase(phase);
    switch (shape) {
        case ModulatorLfoShape::TRIANGLE:
            if (normalized < 0.25f) return normalized * 4.0f;
            if (normalized < 0.75f) return 2.0f - normalized * 4.0f;
            return normalized * 4.0f - 4.0f;
        case ModulatorLfoShape::SAW_UP:
            return normalized * 2.0f - 1.0f;
        case ModulatorLfoShape::SAW_DOWN:
            return 1.0f - normalized * 2.0f;
        case ModulatorLfoShape::SQUARE:
            return normalized < 0.5f ? 1.0f : -1.0f;
        case ModulatorLfoShape::SINE:
        default: {
            // Fast bounded sine approximation: no libm transcendental call in
            // the 128-source control-frame path.
            float x = normalized * 2.0f;
            if (x >= 1.0f) x -= 2.0f;
            float value = 4.0f * x * (1.0f - std::abs(x));
            value += 0.225f * (value * std::abs(value) - value);
            return std::clamp(value, -1.0f, 1.0f);
        }
    }
}

float evaluateProjectAdsrProgress(
    ModulatorAdsrCurve curve,
    float progress
) {
    const float normalized = std::clamp(progress, 0.0f, 1.0f);
    switch (curve) {
        case ModulatorAdsrCurve::SMOOTH:
            return normalized * normalized * (3.0f - 2.0f * normalized);
        case ModulatorAdsrCurve::EXPONENTIAL:
            return normalized * normalized;
        case ModulatorAdsrCurve::LINEAR:
        default:
            return normalized;
    }
}

ProjectControlRuntimeResult evaluateProjectControlRuntimeWithBaseProvider(
    const ProjectModulationRuntimePlan& plan,
    const ProjectCurveArena& arena,
    const ProjectControlTimeSnapshot& time,
    const ProjectModulationTriggerFrame* triggers,
    ProjectLogicalMacroBaseProvider baseProvider,
    void* baseContext,
    ProjectControlRuntimeState& state,
    float* sourceValues,
    uint16_t sourceValueCapacity,
    ProjectLogicalMacroRuntimeSink sink,
    void* sinkContext
) {
    if (baseProvider == nullptr || sourceValues == nullptr || sink == nullptr ||
        sourceValueCapacity < plan.sourceCount ||
        (triggers != nullptr &&
         (triggers->count > triggers->events.size() ||
          triggers->reserved != 0U))) {
        return {ProjectControlRuntimeStatus::INVALID_ARGUMENT};
    }
    if (!validPlanBounds(plan)) {
        return {ProjectControlRuntimeStatus::INVALID_PLAN};
    }
    if (!validTime(time)) {
        return {ProjectControlRuntimeStatus::INVALID_TIME};
    }
    if (!state.initialized || state.sourceCount != plan.sourceCount ||
        state.bindingCount != plan.bindingCount) {
        return {ProjectControlRuntimeStatus::STATE_NOT_SYNCHRONIZED};
    }
    for (uint16_t index = 0; index < plan.sourceCount; ++index) {
        if (state.sources[index].id != plan.sources[index].id) {
            return {ProjectControlRuntimeStatus::STATE_NOT_SYNCHRONIZED};
        }
    }
    for (uint16_t index = 0; index < plan.bindingCount; ++index) {
        if (state.bindingIds[index] != plan.bindings[index].id) {
            return {ProjectControlRuntimeStatus::STATE_NOT_SYNCHRONIZED};
        }
    }

    const uint32_t elapsedMs = time.monotonicMs - state.lastEvaluationMs;
    uint32_t nextSequence = state.frameSequence + 1U;
    if (nextSequence == 0U) nextSequence = 1U;

    uint32_t projectElapsedTick = 0;
    uint16_t projectElapsedFraction = 0;
    elapsedMusicalTime(
        time,
        state.activationMusicalTick,
        state.activationMusicalTickFractionQ16,
        projectElapsedTick,
        projectElapsedFraction
    );

    // Every edge in one drained frame shares this immutable time snapshot.
    // Advance each envelope once, then let ordered edges reuse that exact
    // current level. Only zero-duration stages need another advance while an
    // edge is applied.
    for (uint16_t index = 0U; index < plan.sourceCount; ++index) {
        if (plan.sources[index].kind != ModulatorKind::ADSR) continue;
        sourceValues[index] = advanceAdsrToTime(
            plan.sources[index],
            state.sources[index].payload.adsr,
            time
        );
    }
    if (!routeProjectTriggerFrame(
            plan,
            time,
            triggers,
            state,
            sourceValues
        )) {
        return {ProjectControlRuntimeStatus::INVALID_PLAN};
    }

    for (uint16_t index = 0; index < plan.sourceCount; ++index) {
        const auto& source = plan.sources[index];
        auto& sourceState = state.sources[index];
        float value = 0.0f;
        if (source.kind == ModulatorKind::RECORDED_SHAPE) {
            value = evaluateProjectCurve(
                arena,
                source.parameters.curve,
                projectElapsedTick,
                projectElapsedFraction,
                0.0f,
                &sourceState.payload.recordedCurve
            );
        } else if (source.kind == ModulatorKind::ADSR) {
            value = sourceValues[index];
        } else {
            uint32_t musicalAnchor = state.activationMusicalTick;
            uint16_t musicalAnchorFraction =
                state.activationMusicalTickFractionQ16;
            uint32_t monotonicAnchor = state.activationMonotonicMs;
            if (source.traits.lfo.retrigger ==
                ModulatorRetriggerPolicy::TRANSPORT) {
                musicalAnchor = time.transportStartMusicalTick;
                musicalAnchorFraction = 0U;
                monotonicAnchor = time.transportStartMonotonicMs;
            } else if (
                source.traits.lfo.retrigger ==
                    ModulatorRetriggerPolicy::EXPLICIT_TRIGGER &&
                sourceState.payload.lfo.explicitlyTriggered
            ) {
                musicalAnchor =
                    sourceState.payload.lfo.explicitMusicalAnchorTick;
                musicalAnchorFraction =
                    sourceState.payload.lfo.explicitMusicalAnchorFractionQ16;
                monotonicAnchor =
                    sourceState.payload.lfo.explicitMonotonicAnchorMs;
            }
            const float phase = source.traits.lfo.timing ==
                    ModulatorTimingMode::FREE
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
            value = evaluateProjectLfoShape(source.traits.lfo.shape, phase);
        }
        sourceValues[index] = std::clamp(value, -1.0f, 1.0f);
    }

    ProjectControlRuntimeResult result{
        .status = ProjectControlRuntimeStatus::OK,
        .sourceEvaluationCount = plan.sourceCount,
        .destinationEvaluationCount = plan.destinationCount,
    };
    for (uint16_t destinationIndex = 0;
         destinationIndex < plan.destinationCount;
        ++destinationIndex) {
        const auto& destination = plan.destinations[destinationIndex];
        ProjectLogicalMacroBaseInput baseInput{};
        if (!baseProvider(
                baseContext,
                destinationIndex,
                destination.destination,
                baseInput
            )) {
            return {ProjectControlRuntimeStatus::INVALID_ARGUMENT};
        }
        float base = std::clamp(
            std::isfinite(baseInput.staticValue) ? baseInput.staticValue : 0.0f,
            0.0f,
            1.0f
        );
        uint8_t flags = 0U;
        if (baseInput.manualOverride) {
            base = std::clamp(
                std::isfinite(baseInput.manualValue)
                    ? baseInput.manualValue
                    : base,
                0.0f,
                1.0f
            );
            flags = static_cast<uint8_t>(
                flags | PROJECT_LOGICAL_MACRO_FLAG_MANUAL_OVERRIDE
            );
        } else if (
            (destination.flags &
             PROJECT_CONTROL_RUNTIME_DESTINATION_FLAG_AUTOMATION_ENABLED) != 0U
        ) {
            base = evaluateProjectCurve(
                arena,
                destination.automationCurveRecordIndex,
                projectElapsedTick,
                projectElapsedFraction,
                base
            );
            flags = static_cast<uint8_t>(
                flags | PROJECT_LOGICAL_MACRO_FLAG_AUTOMATION_ACTIVE
            );
        }

        float modulation = 0.0f;
        uint16_t contributionCount = 0;
        for (uint16_t relative = 0;
             relative < destination.bindingCount;
             ++relative) {
            const uint16_t order = static_cast<uint16_t>(
                destination.firstBinding + relative
            );
            const uint16_t bindingIndex = plan.bindingOrder[order];
            const auto& binding = plan.bindings[bindingIndex];
            float contribution = 0.0f;
            if ((binding.flags & PROJECT_MODULATION_BINDING_FLAG_ENABLED) != 0U &&
                binding.sourceIndex < plan.sourceCount &&
                (plan.sources[binding.sourceIndex].flags &
                 PROJECT_MODULATOR_FLAG_ENABLED) != 0U) {
                float sourceValue = sourceValues[binding.sourceIndex];
                sourceValue = applyResolvedModulationMapping(
                    sourceValue,
                    binding.mapping
                );
                const float amount = static_cast<float>(binding.amountQ15) /
                    Q15_SCALE;
                const float target = sourceValue * amount;
                contribution = applySlew(
                    unpackQ15(state.bindingContributionQ15[bindingIndex]),
                    target,
                    binding.slewMs,
                    elapsedMs
                );
                ++contributionCount;
            }
            state.bindingContributionQ15[bindingIndex] = packQ15(contribution);
            modulation += contribution;
        }

        modulation *= static_cast<float>(destination.destinationScaleQ15) /
            static_cast<float>(PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15);

        const float raw = base + modulation;
        const float value = std::clamp(raw, destination.minimum, destination.maximum);
        if (contributionCount > 0U) {
            flags = static_cast<uint8_t>(
                flags | PROJECT_LOGICAL_MACRO_FLAG_MODULATION_ACTIVE
            );
        }
        if (value != raw) {
            flags = static_cast<uint8_t>(
                flags | PROJECT_LOGICAL_MACRO_FLAG_CLIPPED
            );
            ++result.clippedDestinationCount;
        }
        result.contributionCount = static_cast<uint16_t>(
            result.contributionCount + contributionCount
        );
        const ProjectLogicalMacroRuntimeValue published{
            .destination = destination.destination,
            .base = base,
            .modulation = modulation,
            .value = value,
            .contributionCount = contributionCount,
            .flags = flags,
        };
        sink(sinkContext, destinationIndex, published);
    }

    state.lastEvaluationMs = time.monotonicMs;
    state.frameSequence = nextSequence;
    return result;
}

namespace {

struct ArrayBaseProviderContext {
    const ProjectLogicalMacroBaseInput* values = nullptr;
    uint16_t count = 0;
};

bool provideArrayBase(
    void* context,
    uint16_t destinationIndex,
    const ModulationDestination&,
    ProjectLogicalMacroBaseInput& out
) {
    const auto* source = static_cast<const ArrayBaseProviderContext*>(context);
    if (source == nullptr || source->values == nullptr ||
        destinationIndex >= source->count) {
        return false;
    }
    out = source->values[destinationIndex];
    return true;
}

void captureDiagnosticDestination(
    void* context,
    uint16_t destinationIndex,
    const ProjectLogicalMacroRuntimeValue& value
) {
    auto* frame = static_cast<ProjectControlRuntimeFrame*>(context);
    if (frame == nullptr || destinationIndex >= frame->destinations.size()) return;
    frame->destinations[destinationIndex] = value;
}

}  // namespace

ProjectControlRuntimeResult evaluateProjectControlRuntime(
    const ProjectModulationRuntimePlan& plan,
    const ProjectCurveArena& arena,
    const ProjectControlTimeSnapshot& time,
    const ProjectModulationTriggerFrame& triggers,
    const ProjectLogicalMacroBaseInput* bases,
    uint16_t baseCount,
    ProjectControlRuntimeState& state,
    float* sourceValues,
    uint16_t sourceValueCapacity,
    ProjectLogicalMacroRuntimeSink sink,
    void* sinkContext
) {
    ArrayBaseProviderContext context{bases, baseCount};
    return evaluateProjectControlRuntimeWithBaseProvider(
        plan,
        arena,
        time,
        &triggers,
        provideArrayBase,
        &context,
        state,
        sourceValues,
        sourceValueCapacity,
        sink,
        sinkContext
    );
}

ProjectControlRuntimeResult evaluateProjectControlRuntimeFrame(
    const ProjectModulationRuntimePlan& plan,
    const ProjectCurveArena& arena,
    const ProjectControlTimeSnapshot& time,
    const ProjectModulationTriggerFrame& triggers,
    const ProjectLogicalMacroBaseInput* bases,
    uint16_t baseCount,
    ProjectControlRuntimeState& state,
    ProjectControlRuntimeFrame& out
) {
    const auto result = evaluateProjectControlRuntime(
        plan,
        arena,
        time,
        triggers,
        bases,
        baseCount,
        state,
        out.sourceValues.data(),
        static_cast<uint16_t>(out.sourceValues.size()),
        captureDiagnosticDestination,
        &out
    );
    if (result.evaluated()) {
        out.sequence = state.frameSequence;
        out.sourceCount = plan.sourceCount;
        out.destinationCount = plan.destinationCount;
    }
    return result;
}

}  // namespace core::state::modulation

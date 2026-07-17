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
constexpr uint16_t INVALID_CURVE_RECORD =
    std::numeric_limits<uint16_t>::max();

bool validTime(const ProjectControlTimeSnapshot& time) {
    return time.reserved == 0U;
}

bool validPlanBounds(const ProjectModulationRuntimePlan& plan) {
    return plan.sourceCount <= PROJECT_MODULATOR_CAPACITY &&
           plan.bindingCount <= PROJECT_MODULATION_BINDING_CAPACITY &&
           plan.destinationCount <=
               PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY;
}

bool triggerMatches(const ModulationTriggerRef& configured,
                    const ModulationTriggerRef& incoming) {
    return configured.kind == incoming.kind &&
           configured.track == incoming.track &&
           configured.channel == incoming.channel &&
           configured.data == incoming.data;
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
    const ProjectControlTimeSnapshot& time
) {
    return {
        .id = id,
        .explicitMusicalAnchorTick = time.musicalTick,
        .explicitMonotonicAnchorMs = time.monotonicMs,
        .explicitMusicalAnchorFractionQ16 =
            time.musicalTickFractionQ16,
        .explicitlyTriggered = false,
    };
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
        if (state.sources[targetIndex].id == targetId) continue;

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
        state.sources[targetIndex] = makeSourceState(targetId, time);
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
    if (record.pointCount == 0U ||
        record.pointOffset >= arena.pointCount ||
        static_cast<uint32_t>(record.pointOffset) + record.pointCount >
            arena.pointCount) {
        return fallback;
    }

    const uint16_t duration = std::max<uint16_t>(record.durationTicks, 1U);
    const uint16_t sourceDuration = std::max<uint16_t>(
        record.sourceDurationTicks,
        1U
    );
    const uint32_t localWhole = elapsedTick % duration;
    float sourceTick = static_cast<float>(
        (static_cast<uint32_t>(record.windowOffsetTicks) + localWhole) %
        sourceDuration
    );
    sourceTick += static_cast<float>(elapsedFractionQ16) / Q16_SCALE;
    if (sourceTick >= static_cast<float>(sourceDuration)) {
        sourceTick -= static_cast<float>(sourceDuration);
    }

    const uint16_t firstIndex = record.pointOffset;
    const uint16_t lastIndex = static_cast<uint16_t>(
        record.pointOffset + record.pointCount - 1U
    );
    const auto& first = arena.points[firstIndex];
    if (record.pointCount == 1U || sourceTick <= first.tick) {
        return unpackProjectCurveValue(first.value, record.valueDomain);
    }
    const auto& last = arena.points[lastIndex];
    if (sourceTick >= last.tick) {
        return unpackProjectCurveValue(last.value, record.valueDomain);
    }

    uint16_t low = 1U;
    uint16_t high = record.pointCount;
    while (low < high) {
        const uint16_t mid = static_cast<uint16_t>(
            low + (high - low) / 2U
        );
        if (arena.points[static_cast<uint16_t>(record.pointOffset + mid)].tick <
            sourceTick) {
            low = static_cast<uint16_t>(mid + 1U);
        } else {
            high = mid;
        }
    }
    const uint16_t rightIndex = static_cast<uint16_t>(
        record.pointOffset + low
    );
    const uint16_t leftIndex = static_cast<uint16_t>(rightIndex - 1U);
    const auto& left = arena.points[leftIndex];
    const auto& right = arena.points[rightIndex];
    const float leftValue = unpackProjectCurveValue(
        left.value,
        record.valueDomain
    );
    const float rightValue = unpackProjectCurveValue(
        right.value,
        record.valueDomain
    );
    const uint16_t span = static_cast<uint16_t>(right.tick - left.tick);
    if (span == 0U) return rightValue;
    const float alpha = std::clamp(
        (sourceTick - static_cast<float>(left.tick)) /
            static_cast<float>(span),
        0.0f,
        1.0f
    );
    const float value = leftValue + (rightValue - leftValue) * alpha;
    return record.valueDomain == ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR
        ? std::clamp(value, 0.0f, 1.0f)
        : std::clamp(value, -1.0f, 1.0f);
}

int16_t packQ15(float value) {
    return static_cast<int16_t>(std::lround(
        std::clamp(value, -1.0f, 1.0f) * Q15_SCALE
    ));
}

float unpackQ15(int16_t value) {
    return std::clamp(static_cast<float>(value) / Q15_SCALE, -1.0f, 1.0f);
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

    for (uint16_t index = 0; index < plan.sourceCount; ++index) {
        const auto& source = plan.sources[index];
        auto& sourceState = state.sources[index];
        if (source.retrigger == ModulatorRetriggerPolicy::EXPLICIT_TRIGGER &&
            (source.triggerFlags & PROJECT_MODULATION_TRIGGER_FLAG_ENABLED) != 0U) {
            for (uint16_t eventIndex = 0;
                 triggers != nullptr && eventIndex < triggers->count;
                 ++eventIndex) {
                if (!triggerMatches(
                        source.trigger,
                        triggers->events[eventIndex]
                    )) {
                    continue;
                }
                sourceState.explicitMusicalAnchorTick = time.musicalTick;
                sourceState.explicitMonotonicAnchorMs = time.monotonicMs;
                sourceState.explicitMusicalAnchorFractionQ16 =
                    time.musicalTickFractionQ16;
                sourceState.explicitlyTriggered = true;
                break;
            }
        }

        float value = 0.0f;
        if (source.kind == ModulatorKind::RECORDED_SHAPE) {
            value = evaluateProjectCurve(
                arena,
                source.curveRecordIndex,
                projectElapsedTick,
                projectElapsedFraction,
                0.0f
            );
        } else {
            uint32_t musicalAnchor = state.activationMusicalTick;
            uint16_t musicalAnchorFraction =
                state.activationMusicalTickFractionQ16;
            uint32_t monotonicAnchor = state.activationMonotonicMs;
            if (source.retrigger == ModulatorRetriggerPolicy::TRANSPORT) {
                musicalAnchor = time.transportStartMusicalTick;
                musicalAnchorFraction = 0U;
                monotonicAnchor = time.transportStartMonotonicMs;
            } else if (
                source.retrigger == ModulatorRetriggerPolicy::EXPLICIT_TRIGGER &&
                sourceState.explicitlyTriggered
            ) {
                musicalAnchor = sourceState.explicitMusicalAnchorTick;
                musicalAnchorFraction =
                    sourceState.explicitMusicalAnchorFractionQ16;
                monotonicAnchor = sourceState.explicitMonotonicAnchorMs;
            }
            const float phase = source.timing == ModulatorTimingMode::FREE
                ? phaseFromFreeTime(
                    time.monotonicMs,
                    monotonicAnchor,
                    source.freePeriodMs,
                    source.phaseQ15
                )
                : phaseFromMusicalTime(
                    time,
                    musicalAnchor,
                    musicalAnchorFraction,
                    source.periodTicks,
                    source.phaseQ15
                );
            value = evaluateProjectLfoShape(source.shape, phase);
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

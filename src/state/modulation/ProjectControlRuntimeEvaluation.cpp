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

}  // namespace project_control_runtime_detail

using namespace project_control_runtime_detail;

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
         triggers->count > triggers->events.size())) {
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
    // Copy the cold session descriptor once. The per-source/per-destination
    // hot loops then read stack-local bytes instead of repeatedly touching the
    // PSRAM-backed control state.
    const auto recordedShapeAudition = state.recordedShapeAudition;

    // Every edge in one drained frame shares this immutable time snapshot.
    // Advance each envelope once, then let ordered edges reuse that exact
    // current level. Only zero-duration stages need another advance while an
    // edge is applied.
    for (uint16_t index = 0U; index < plan.sourceCount; ++index) {
        if (plan.sources[index].kind != ModulatorKind::ADSR) continue;
        sourceValues[index] = advanceAdsrRawToTime(
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
            value = unpackQ15(advanceAdsrSmoothQ15(
                source,
                sourceState.payload.adsr,
                time,
                state.lastEvaluationMs,
                state.lastEvaluationMusicalTick,
                state.lastEvaluationMusicalTickFractionQ16,
                sourceValues[index]
            ));
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
        if (recordedShapeAudition.mode ==
                ProjectRecordedShapeRuntimeAuditionMode::SOURCE_OVERRIDE &&
            recordedShapeAudition.sourceId == source.id) {
            value = unpackQ15(recordedShapeAudition.sourceValueQ15);
        }
        sourceValues[index] = std::clamp(value, -1.0f, 1.0f);
    }

    ProjectControlRuntimeResult result{
        .status = ProjectControlRuntimeStatus::OK,
        .sourceEvaluationCount = plan.sourceCount,
        .destinationEvaluationCount = plan.destinationCount,
    };
    bool auditionDestinationEvaluated = false;
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
        float underlyingBase = std::clamp(
            std::isfinite(baseInput.staticValue) ? baseInput.staticValue : 0.0f,
            0.0f,
            1.0f
        );
        uint8_t flags = 0U;
        if ((destination.flags &
             PROJECT_CONTROL_RUNTIME_DESTINATION_FLAG_AUTOMATION_ENABLED) != 0U) {
            underlyingBase = evaluateProjectCurve(
                arena,
                destination.automationCurveRecordIndex,
                projectElapsedTick,
                projectElapsedFraction,
                underlyingBase
            );
            flags = static_cast<uint8_t>(
                flags | PROJECT_LOGICAL_MACRO_FLAG_AUTOMATION_ACTIVE
            );
        }
        float base = underlyingBase;
        if (baseInput.manualOverride) {
            base = std::clamp(
                std::isfinite(baseInput.manualValue)
                    ? baseInput.manualValue
                    : underlyingBase,
                0.0f,
                1.0f
            );
            flags = static_cast<uint8_t>(
                flags | PROJECT_LOGICAL_MACRO_FLAG_MANUAL_OVERRIDE
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

        if (recordedShapeAudition.mode ==
                ProjectRecordedShapeRuntimeAuditionMode::DESTINATION_ADD &&
            recordedShapeAudition.destination == destination.destination) {
            auditionDestinationEvaluated = true;
            modulation +=
                unpackQ15(recordedShapeAudition.sourceValueQ15) *
                unpackQ15(recordedShapeAudition.amountQ15);
            ++contributionCount;
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
            .underlyingBase = underlyingBase,
            .base = base,
            .modulation = modulation,
            .value = value,
            .contributionCount = contributionCount,
            .flags = flags,
        };
        sink(sinkContext, destinationIndex, published);
    }

    // A destination-first capture may target a perfectly valid static Macro
    // that has no compiled Automation or Modulation edge yet. Publish that one
    // provisional destination through the same resolver/sink instead of
    // creating a second MIDI or UI path in the input handler.
    if (recordedShapeAudition.mode ==
            ProjectRecordedShapeRuntimeAuditionMode::DESTINATION_ADD &&
        !auditionDestinationEvaluated) {
        ProjectLogicalMacroBaseInput baseInput{};
        if (!baseProvider(
                baseContext,
                plan.destinationCount,
                recordedShapeAudition.destination,
                baseInput
            )) {
            return {ProjectControlRuntimeStatus::INVALID_ARGUMENT};
        }
        const float underlyingBase = std::clamp(
            std::isfinite(baseInput.staticValue) ? baseInput.staticValue : 0.0f,
            0.0f,
            1.0f
        );
        float base = underlyingBase;
        uint8_t flags = PROJECT_LOGICAL_MACRO_FLAG_MODULATION_ACTIVE;
        if (baseInput.manualOverride) {
            base = std::clamp(
                std::isfinite(baseInput.manualValue)
                    ? baseInput.manualValue
                    : underlyingBase,
                0.0f,
                1.0f
            );
            flags = static_cast<uint8_t>(
                flags | PROJECT_LOGICAL_MACRO_FLAG_MANUAL_OVERRIDE
            );
        }
        const float modulation =
            unpackQ15(recordedShapeAudition.sourceValueQ15) *
            unpackQ15(recordedShapeAudition.amountQ15) *
            (static_cast<float>(recordedShapeAudition.destinationScaleQ15) /
             static_cast<float>(
                 PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15
             ));
        const float raw = base + modulation;
        const float value = std::clamp(raw, 0.0f, 1.0f);
        if (value != raw) {
            flags = static_cast<uint8_t>(
                flags | PROJECT_LOGICAL_MACRO_FLAG_CLIPPED
            );
            ++result.clippedDestinationCount;
        }
        ++result.destinationEvaluationCount;
        ++result.contributionCount;
        sink(
            sinkContext,
            plan.destinationCount,
            ProjectLogicalMacroRuntimeValue{
                .destination = recordedShapeAudition.destination,
                .underlyingBase = underlyingBase,
                .base = base,
                .modulation = modulation,
                .value = value,
                .contributionCount = 1U,
                .flags = flags,
            }
        );
    }

    state.lastEvaluationMs = time.monotonicMs;
    state.lastEvaluationMusicalTick = time.musicalTick;
    state.lastEvaluationMusicalTickFractionQ16 =
        time.musicalTickFractionQ16;
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
        out.destinationCount = result.destinationEvaluationCount;
    }
    return result;
}

}  // namespace core::state::modulation

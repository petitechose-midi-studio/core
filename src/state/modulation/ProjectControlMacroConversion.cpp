#include "state/modulation/ProjectControlMacroOps.hpp"

#include <algorithm>
#include <cmath>

#include <config/PlatformCompat.hpp>

#include "app/ExtmemAllocator.hpp"
#include "state/modulation/ProjectControlMacroOpsInternal.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation {

using namespace project_control_macro_detail;

namespace {

ProjectCurveOrigin projectOrigin(ProjectAutomationConversionPolicy policy) {
    switch (policy) {
        case ProjectAutomationConversionPolicy::MEAN:
            return ProjectCurveOrigin::CONVERTED_MEAN;
        case ProjectAutomationConversionPolicy::FIRST:
            return ProjectCurveOrigin::CONVERTED_FIRST;
        case ProjectAutomationConversionPolicy::MIN:
            return ProjectCurveOrigin::CONVERTED_MIN;
        default:
            return ProjectCurveOrigin::NATIVE;
    }
}

constexpr uint32_t FNV_OFFSET_BASIS = 2166136261U;
constexpr uint32_t FNV_PRIME = 16777619U;

FLASHMEM uint32_t fingerprintByte(uint32_t value, uint8_t byte) {
    return (value ^ byte) * FNV_PRIME;
}

FLASHMEM uint32_t fingerprintU16(uint32_t value, uint16_t item) {
    value = fingerprintByte(value, static_cast<uint8_t>(item & 0xFFU));
    return fingerprintByte(value, static_cast<uint8_t>(item >> 8U));
}

FLASHMEM uint32_t fingerprintU32(uint32_t value, uint32_t item) {
    value = fingerprintU16(value, static_cast<uint16_t>(item & 0xFFFFU));
    return fingerprintU16(value, static_cast<uint16_t>(item >> 16U));
}

FLASHMEM float unpackAbsolute(int16_t packed) {
    return std::clamp(static_cast<float>(packed) / 32767.0f, 0.0f, 1.0f);
}

FLASHMEM int16_t packBipolar(float value) {
    const long packed = std::lround(
        std::clamp(value, -1.0f, 1.0f) * 32767.0f
    );
    return static_cast<int16_t>(std::clamp<long>(
        packed,
        -32767L,
        32767L
    ));
}

FLASHMEM bool conversionPolicyValid(ProjectAutomationConversionPolicy policy) {
    return policy == ProjectAutomationConversionPolicy::MEAN ||
           policy == ProjectAutomationConversionPolicy::FIRST ||
           policy == ProjectAutomationConversionPolicy::MIN;
}

FLASHMEM bool curveRangeValid(
    const ProjectCurveArena& arena,
    const ProjectCurveRecord& curve
) {
    return curve.pointCount > 0U &&
           static_cast<uint32_t>(curve.pointOffset) + curve.pointCount <=
               arena.pointCount;
}

FLASHMEM double curveIntegralToTick(
    const ProjectCurveArena& arena,
    const ProjectCurveRecord& curve,
    uint16_t targetTick
) {
    if (!curveRangeValid(arena, curve) || targetTick == 0U) return 0.0;
    const auto pointAt = [&](uint16_t index)
        -> const ProjectPackedCurvePoint& {
        return arena.points[static_cast<uint16_t>(curve.pointOffset + index)];
    };
    const auto valueAt = [&](uint16_t index) {
        return static_cast<double>(unpackAbsolute(pointAt(index).value));
    };

    const auto& first = pointAt(0U);
    const double firstValue = valueAt(0U);
    if (targetTick <= first.tick) {
        return firstValue * static_cast<double>(targetTick);
    }
    double integral = firstValue * static_cast<double>(first.tick);
    for (uint16_t index = 1U; index < curve.pointCount; ++index) {
        const auto& previous = pointAt(static_cast<uint16_t>(index - 1U));
        const auto& current = pointAt(index);
        const double previousValue = valueAt(
            static_cast<uint16_t>(index - 1U)
        );
        const double currentValue = valueAt(index);
        if (targetTick <= current.tick) {
            const uint16_t elapsed = static_cast<uint16_t>(
                targetTick - previous.tick
            );
            const uint16_t span = static_cast<uint16_t>(
                current.tick - previous.tick
            );
            const double endValue = span == 0U
                ? currentValue
                : previousValue + (currentValue - previousValue) *
                    (static_cast<double>(elapsed) /
                     static_cast<double>(span));
            return integral + (previousValue + endValue) * 0.5 *
                static_cast<double>(elapsed);
        }
        integral += (previousValue + currentValue) * 0.5 *
            static_cast<double>(current.tick - previous.tick);
    }

    const auto& last = pointAt(static_cast<uint16_t>(
        curve.pointCount - 1U
    ));
    if (targetTick > last.tick) {
        integral += valueAt(static_cast<uint16_t>(curve.pointCount - 1U)) *
            static_cast<double>(targetTick - last.tick);
    }
    return integral;
}

FLASHMEM float timeWeightedCurveMean(
    const ProjectCurveArena& arena,
    const ProjectCurveRecord& curve
) {
    if (!curveRangeValid(arena, curve)) return 0.0f;
    const auto& last = arena.points[static_cast<uint16_t>(
        curve.pointOffset + curve.pointCount - 1U
    )];
    const uint16_t sourceTicks = std::max<uint16_t>({
        curve.sourceDurationTicks,
        last.tick,
        1U,
    });
    const uint16_t durationTicks = std::max<uint16_t>(
        curve.durationTicks,
        1U
    );
    const uint16_t offset = static_cast<uint16_t>(
        curve.windowOffsetTicks % sourceTicks
    );
    const double cycleIntegral = curveIntegralToTick(
        arena,
        curve,
        sourceTicks
    );
    const uint32_t fullCycles = durationTicks / sourceTicks;
    uint16_t remainder = static_cast<uint16_t>(durationTicks % sourceTicks);
    double integral = cycleIntegral * static_cast<double>(fullCycles);
    if (remainder > 0U) {
        const uint16_t firstLength = std::min<uint16_t>(
            remainder,
            static_cast<uint16_t>(sourceTicks - offset)
        );
        integral += curveIntegralToTick(
            arena,
            curve,
            static_cast<uint16_t>(offset + firstLength)
        ) - curveIntegralToTick(arena, curve, offset);
        remainder = static_cast<uint16_t>(remainder - firstLength);
        if (remainder > 0U) {
            integral += curveIntegralToTick(arena, curve, remainder);
        }
    }
    return std::clamp(
        static_cast<float>(integral / static_cast<double>(durationTicks)),
        0.0f,
        1.0f
    );
}

FLASHMEM float conversionReference(
    const ProjectCurveArena& arena,
    const ProjectCurveRecord& curve,
    ProjectAutomationConversionPolicy policy
) {
    if (!curveRangeValid(arena, curve)) return 0.0f;
    if (policy == ProjectAutomationConversionPolicy::FIRST) {
        return unpackAbsolute(arena.points[curve.pointOffset].value);
    }
    if (policy == ProjectAutomationConversionPolicy::MIN) {
        int16_t minimum = 32767;
        for (uint16_t index = 0U; index < curve.pointCount; ++index) {
            minimum = std::min(
                minimum,
                arena.points[static_cast<uint16_t>(
                    curve.pointOffset + index
                )].value
            );
        }
        return unpackAbsolute(minimum);
    }
    return timeWeightedCurveMean(arena, curve);
}

FLASHMEM float conversionAmplitude(
    const ProjectCurveArena& arena,
    const ProjectCurveRecord& curve,
    float reference
) {
    if (!curveRangeValid(arena, curve)) return 0.0f;
    float amplitude = 0.0f;
    for (uint16_t index = 0U; index < curve.pointCount; ++index) {
        const float value = unpackAbsolute(
            arena.points[static_cast<uint16_t>(
                curve.pointOffset + index
            )].value
        );
        amplitude = std::max(amplitude, std::fabs(value - reference));
    }
    return std::clamp(amplitude, 0.0f, 1.0f);
}

FLASHMEM uint32_t curveFingerprint(
    const ProjectCurveArena& arena,
    const ProjectCurveRecord& curve,
    bool enabled
) {
    uint32_t value = FNV_OFFSET_BASIS;
    value = fingerprintU32(value, curve.id.value);
    value = fingerprintByte(value, enabled ? 1U : 0U);
    value = fingerprintU16(value, curve.pointCount);
    value = fingerprintU16(value, curve.sourceDurationTicks);
    value = fingerprintU16(value, curve.durationTicks);
    value = fingerprintU16(value, curve.windowOffsetTicks);
    value = fingerprintByte(
        value,
        static_cast<uint8_t>(curve.interpolation)
    );
    value = fingerprintByte(value, static_cast<uint8_t>(curve.valueDomain));
    value = fingerprintByte(value, static_cast<uint8_t>(curve.origin));
    if (!curveRangeValid(arena, curve)) return value;
    for (uint16_t index = 0U; index < curve.pointCount; ++index) {
        const auto& point = arena.points[static_cast<uint16_t>(
            curve.pointOffset + index
        )];
        value = fingerprintU16(value, point.tick);
        value = fingerprintU16(value, static_cast<uint16_t>(point.value));
    }
    return value;
}

FLASHMEM uint32_t destinationFingerprint(
    const ProjectControlDomainState& domain,
    const ModulationDestination& destination,
    uint16_t& bindingCount
) {
    uint32_t value = FNV_OFFSET_BASIS;
    bindingCount = 0U;
    for (uint16_t index = 0U;
         index < domain.modulation.outputBindingCount;
         ++index) {
        const auto& binding = domain.modulation.outputBindings[index];
        if (binding.destination != destination) continue;
        ++bindingCount;
        value = fingerprintU32(value, binding.id.value);
        value = fingerprintU32(value, binding.sourceId.value);
        value = fingerprintU16(
            value,
            static_cast<uint16_t>(binding.amountQ15)
        );
        value = fingerprintByte(
            value,
            static_cast<uint8_t>(binding.application)
        );
        value = fingerprintByte(
            value,
            static_cast<uint8_t>(binding.transfer)
        );
        value = fingerprintU16(value, binding.slewMs);
        value = fingerprintByte(value, binding.flags);
    }
    value = fingerprintU16(
        value,
        projectModulationDestinationScaleQ15(
            domain.modulation,
            destination
        )
    );
    return fingerprintU16(value, bindingCount);
}

FLASHMEM bool removeDestinationBindings(
    ProjectModulationState& state,
    const ModulationDestination& destination
) {
    for (uint16_t cursor = 0U; cursor < state.outputBindingCount;) {
        if (state.outputBindings[cursor].destination != destination) {
            ++cursor;
            continue;
        }
        const auto id = state.outputBindings[cursor].id;
        if (!removeProjectModulationBinding(state, id).changed()) {
            return false;
        }
    }
    return true;
}

}  // namespace

FLASHMEM ProjectAutomationConversionPlan preflightProjectControlConversion(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ProjectAutomationConversionPolicy policy,
    float currentStaticBase
) {
    ProjectAutomationConversionPlan plan{};
    plan.address = address;
    plan.policy = policy;
    plan.expectedStaticBase = std::clamp(currentStaticBase, 0.0f, 1.0f);
    if (!validAddress(address) || !conversionPolicyValid(policy)) {
        plan.status = ProjectAutomationConversionStatus::INVALID_ADDRESS;
        return plan;
    }
    if (!validProjectModulationDomain(
            control.authored.modulation,
            control.authored.curves,
            &control.authored.automation
        )) {
        plan.status = ProjectAutomationConversionStatus::INVALID_DOMAIN;
        return plan;
    }

    const auto destination = projectControlDestination(address);
    const auto* automation = findProjectAutomationCurve(
        control.authored.automation,
        destination
    );
    if (automation == nullptr) {
        plan.status = ProjectAutomationConversionStatus::NO_AUTOMATION;
        return plan;
    }
    const auto* curve = findProjectCurve(
        control.authored.curves,
        automation->curveId
    );
    if (curve == nullptr ||
        curve->valueDomain !=
            ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR ||
        !curveRangeValid(control.authored.curves, *curve)) {
        plan.status = ProjectAutomationConversionStatus::INVALID_DOMAIN;
        return plan;
    }

    plan.pointCount = curve->pointCount;
    plan.freePointCount = static_cast<uint16_t>(
        PROJECT_CURVE_POINT_CAPACITY - control.authored.curves.pointCount
    );
    plan.reference = conversionReference(
        control.authored.curves,
        *curve,
        policy
    );
    plan.normalizationAmplitude = conversionAmplitude(
        control.authored.curves,
        *curve,
        plan.reference
    );
    plan.sourceFingerprint = curveFingerprint(
        control.authored.curves,
        *curve,
        (automation->flags & PROJECT_AUTOMATION_CURVE_FLAG_ENABLED) != 0U
    );
    plan.targetFingerprint = destinationFingerprint(
        control.authored,
        destination,
        plan.existingBindingCount
    );
    plan.overwritesModulation = plan.existingBindingCount > 0U;

    if (control.authored.modulation.sourceCount >=
            PROJECT_MODULATOR_CAPACITY ||
        control.authored.modulation.nextSourceId == 0U) {
        plan.status =
            ProjectAutomationConversionStatus::SOURCE_CAPACITY_EXHAUSTED;
        return plan;
    }
    const uint32_t nextBindingCount =
        static_cast<uint32_t>(
            control.authored.modulation.outputBindingCount
        ) - plan.existingBindingCount + 1U;
    if (nextBindingCount > PROJECT_MODULATION_BINDING_CAPACITY ||
        control.authored.modulation.nextBindingId == 0U) {
        plan.status =
            ProjectAutomationConversionStatus::BINDING_CAPACITY_EXHAUSTED;
        return plan;
    }
    if (control.authored.curves.recordCount >=
            PROJECT_CURVE_LIVE_CAPACITY ||
        control.authored.curves.recordCount >=
            PROJECT_CURVE_RECORD_CAPACITY ||
        control.authored.curves.nextCurveId == 0U) {
        plan.status =
            ProjectAutomationConversionStatus::CURVE_CAPACITY_EXHAUSTED;
        return plan;
    }
    if (plan.pointCount > plan.freePointCount) {
        plan.status =
            ProjectAutomationConversionStatus::POINT_CAPACITY_EXHAUSTED;
        return plan;
    }
    plan.status = plan.overwritesModulation
        ? ProjectAutomationConversionStatus::OVERWRITE_REQUIRED
        : ProjectAutomationConversionStatus::READY;
    return plan;
}

FLASHMEM bool applyProjectControlConversion(
    ProjectControlState& control,
    float& staticBase,
    const ProjectAutomationConversionPlan& plan,
    bool overwriteConfirmed
) {
    if (!plan.actionable()) return false;
    const float currentBase = std::clamp(staticBase, 0.0f, 1.0f);
    if (std::fabs(currentBase - plan.expectedStaticBase) > 0.000001f) {
        return false;
    }
    const auto current = preflightProjectControlConversion(
        control,
        plan.address,
        plan.policy,
        currentBase
    );
    if (!current.actionable() ||
        current.sourceFingerprint != plan.sourceFingerprint ||
        current.targetFingerprint != plan.targetFingerprint ||
        current.pointCount != plan.pointCount ||
        current.existingBindingCount != plan.existingBindingCount ||
        current.overwritesModulation != plan.overwritesModulation ||
        std::fabs(current.reference - plan.reference) > 0.000001f ||
        std::fabs(
            current.normalizationAmplitude - plan.normalizationAmplitude
        ) > 0.000001f ||
        (current.overwritesModulation && !overwriteConfirmed)) {
        return false;
    }

    const auto destination = projectControlDestination(plan.address);
    const auto* automation = findProjectAutomationCurve(
        control.authored.automation,
        destination
    );
    const auto* curve = automation != nullptr
        ? findProjectCurve(control.authored.curves, automation->curveId)
        : nullptr;
    if (curve == nullptr || curve->pointCount != plan.pointCount) {
        return false;
    }

    auto normalized = core::app::makeExtmemUniqueArrayForOverwrite<
        ProjectPackedCurvePoint
    >(curve->pointCount);
    auto pending = core::app::makeExtmemUnique<ProjectControlDomainState>();
    if (!normalized || !pending) return false;
    for (uint16_t index = 0U; index < curve->pointCount; ++index) {
        const auto& point = control.authored.curves.points[
            static_cast<uint16_t>(curve->pointOffset + index)
        ];
        const float absolute = unpackAbsolute(point.value);
        const float relative = current.normalizationAmplitude > 0.000001f
            ? (absolute - current.reference) /
                current.normalizationAmplitude
            : 0.0f;
        normalized[index] = {
            .tick = point.tick,
            .value = packBipolar(relative),
        };
    }

    *pending = control.authored;
    if (!removeDestinationBindings(pending->modulation, destination)) {
        return false;
    }
    ProjectControlCurvePayload recordedShape{
        .spec = {
            .sourceDurationTicks = curve->sourceDurationTicks,
            .durationTicks = curve->durationTicks,
            .windowOffsetTicks = curve->windowOffsetTicks,
            .interpolation = curve->interpolation,
            .valueDomain = ProjectCurveValueDomain::BIPOLAR,
            .origin = projectOrigin(plan.policy),
        },
        .pointOffset = 0U,
        .pointCount = curve->pointCount,
        .enabled = true,
    };
    if (!appendRecordedShape(
            *pending,
            plan.address,
            recordedShape,
            current.normalizationAmplitude,
            normalized.get()
        )) {
        return false;
    }
    const auto disabled = setProjectAutomationEnabled(
        pending->automation,
        destination,
        false
    );
    if (!disabled.changed() &&
        disabled.status != ProjectModulationStatus::NO_CHANGE) {
        return false;
    }
    if (!validProjectModulationDomain(
            pending->modulation,
            pending->curves,
            &pending->automation
        )) {
        return false;
    }

    control.authored = *pending;
    control.markAuthoredMutation();
    staticBase = current.reference;
    return true;
}

}  // namespace core::state::modulation

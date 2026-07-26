#include "state/modulation/ProjectModulationDomainOps.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectModulationDomainOpsInternal.hpp"

namespace core::state::modulation {

using namespace project_modulation_detail;
FLASHMEM bool validProjectModulatorAdsrParameters(
    const ModulatorAdsrParameters& parameters
) {
    return validAdsrParameters(parameters);
}

FLASHMEM bool validProjectCurveSpec(
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
) {
    if (points == nullptr || pointCount == 0 ||
        spec.sourceDurationTicks == 0 || spec.durationTicks == 0 ||
        spec.windowOffsetTicks > spec.sourceDurationTicks ||
        spec.interpolation != ProjectCurveInterpolation::LINEAR ||
        static_cast<uint8_t>(spec.valueDomain) >
            static_cast<uint8_t>(ProjectCurveValueDomain::BIPOLAR) ||
        static_cast<uint8_t>(spec.origin) >
            static_cast<uint8_t>(ProjectCurveOrigin::CONVERTED_MIN)) {
        return false;
    }
    for (uint16_t index = 0; index < pointCount; ++index) {
        if (points[index].tick > spec.sourceDurationTicks ||
            points[index].value == std::numeric_limits<int16_t>::min() ||
            (spec.valueDomain == ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR &&
             points[index].value < 0)) {
            return false;
        }
        if (index > 0 && points[index - 1U].tick > points[index].tick) {
            return false;
        }
    }
    return true;
}

FLASHMEM bool projectModulatorNaturalDomain(
    const ModulatorSourceState& source,
    const ProjectCurveArena& arena,
    ModulatorNaturalDomain& out
) {
    if (source.kind == ModulatorKind::LFO) {
        out = ModulatorNaturalDomain::CENTERED;
        return true;
    }
    if (source.kind == ModulatorKind::ADSR) {
        out = ModulatorNaturalDomain::POSITIVE;
        return true;
    }
    if (source.kind != ModulatorKind::RECORDED_SHAPE) return false;
    const auto* curve = findProjectCurve(
        arena,
        source.parameters.recordedCurveId
    );
    if (curve == nullptr) return false;
    switch (curve->valueDomain) {
        case ProjectCurveValueDomain::BIPOLAR:
            out = ModulatorNaturalDomain::CENTERED;
            return true;
        case ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR:
            out = ModulatorNaturalDomain::POSITIVE;
            return true;
        default:
            return false;
    }
}

FLASHMEM bool resolveModulationApplication(
    ModulationApplication application,
    ModulatorNaturalDomain naturalDomain,
    ResolvedModulationMapping& out
) {
    if (static_cast<uint8_t>(naturalDomain) >
        static_cast<uint8_t>(ModulatorNaturalDomain::POSITIVE)) {
        return false;
    }
    switch (application) {
        case ModulationApplication::NATURAL:
            out = ResolvedModulationMapping::IDENTITY;
            return true;
        case ModulationApplication::AROUND_BASE:
            out = naturalDomain == ModulatorNaturalDomain::CENTERED
                ? ResolvedModulationMapping::IDENTITY
                : ResolvedModulationMapping::POSITIVE_TO_CENTERED;
            return true;
        case ModulationApplication::FROM_BASE:
            out = naturalDomain == ModulatorNaturalDomain::POSITIVE
                ? ResolvedModulationMapping::IDENTITY
                : ResolvedModulationMapping::CENTERED_TO_POSITIVE;
            return true;
        default:
            return false;
    }
}

FLASHMEM const ModulatorSourceState* findProjectModulator(
    const ProjectModulationState& state,
    ModulatorId id
) {
    const int16_t index = sourceIndex(state, id);
    return index < 0 ? nullptr : &state.sources[static_cast<uint16_t>(index)];
}

FLASHMEM ModulatorSourceState* findProjectModulator(
    ProjectModulationState& state,
    ModulatorId id
) {
    const int16_t index = sourceIndex(state, id);
    return index < 0 ? nullptr : &state.sources[static_cast<uint16_t>(index)];
}

FLASHMEM const ModulationBindingState* findProjectModulationBinding(
    const ProjectModulationState& state,
    ModulationBindingId id
) {
    const int16_t index = outputBindingIndex(state, id);
    return index >= 0
        ? &state.outputBindings[static_cast<uint16_t>(index)]
        : nullptr;
}

FLASHMEM ModulationBindingState* findProjectModulationBinding(
    ProjectModulationState& state,
    ModulationBindingId id
) {
    const int16_t index = outputBindingIndex(state, id);
    return index >= 0
        ? &state.outputBindings[static_cast<uint16_t>(index)]
        : nullptr;
}

FLASHMEM const ModulationTriggerBindingState*
findProjectModulationTriggerForSource(
    const ProjectModulationState& state,
    ModulatorId sourceId
) {
    const int16_t index = triggerIndexForSource(state, sourceId);
    return index < 0
        ? nullptr
        : &state.triggerBindings[static_cast<uint16_t>(index)];
}

FLASHMEM ModulationTriggerBindingState* findProjectModulationTriggerForSource(
    ProjectModulationState& state,
    ModulatorId sourceId
) {
    const int16_t index = triggerIndexForSource(state, sourceId);
    return index < 0
        ? nullptr
        : &state.triggerBindings[static_cast<uint16_t>(index)];
}

FLASHMEM const ModulationDestinationScaleState*
findProjectModulationDestinationScale(
    const ProjectModulationState& state,
    const ModulationDestination& destination
) {
    if (!modulationDestinationValid(destination)) return nullptr;
    const int16_t index = destinationScaleIndex(state, destination);
    return index < 0
        ? nullptr
        : &state.destinationScales[static_cast<uint16_t>(index)];
}

FLASHMEM uint16_t projectModulationDestinationScaleQ15(
    const ProjectModulationState& state,
    const ModulationDestination& destination
) {
    const auto* entry = findProjectModulationDestinationScale(
        state,
        destination
    );
    return entry != nullptr
        ? entry->scaleQ15
        : PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
}

FLASHMEM void formatNextProjectLfoName(
    const ProjectModulationState& state,
    char* out,
    size_t outSize
) {
    formatNextProjectModulatorName(state, ModulatorKind::LFO, out, outSize);
}

FLASHMEM void formatNextProjectModulatorName(
    const ProjectModulationState& state,
    ModulatorKind kind,
    char* out,
    size_t outSize
) {
    if (out == nullptr || outSize == 0U) return;
    const char* prefix = "Motion";
    if (kind == ModulatorKind::LFO) {
        prefix = "LFO";
    } else if (kind == ModulatorKind::ADSR) {
        prefix = "DAHDSR";
    }
    for (uint16_t ordinal = 1; ordinal <= PROJECT_MODULATOR_CAPACITY; ++ordinal) {
        std::snprintf(
            out,
            outSize,
            "%s %u",
            prefix,
            static_cast<unsigned>(ordinal)
        );
        bool used = false;
        for (uint16_t index = 0; index < state.sourceCount; ++index) {
            if (std::strncmp(
                    state.sources[index].name.data(),
                    out,
                    PROJECT_MODULATOR_NAME_CAPACITY
                ) == 0) {
                used = true;
                break;
            }
        }
        if (!used) return;
    }
    std::snprintf(out, outSize, "%s", prefix);
}

FLASHMEM const ProjectCurveRecord* findProjectCurve(
    const ProjectCurveArena& arena,
    ProjectCurveId id
) {
    const int16_t index = curveIndex(arena, id);
    return index < 0 ? nullptr : &arena.records[static_cast<uint16_t>(index)];
}

FLASHMEM const ProjectAutomationCurveEntry* findProjectAutomationCurve(
    const ProjectAutomationCurveDirectory& automation,
    const ModulationDestination& destination
) {
    for (uint16_t index = 0; index < automation.entryCount; ++index) {
        if (automation.entries[index].destination == destination) {
            return &automation.entries[index];
        }
    }
    return nullptr;
}

FLASHMEM ProjectAutomationCurveEntry* findProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    const ModulationDestination& destination
) {
    return const_cast<ProjectAutomationCurveEntry*>(findProjectAutomationCurve(
        static_cast<const ProjectAutomationCurveDirectory&>(automation),
        destination
    ));
}

}  // namespace core::state::modulation

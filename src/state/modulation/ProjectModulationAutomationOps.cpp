#include "state/modulation/ProjectModulationDomainOps.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectModulationDomainOpsInternal.hpp"

namespace core::state::modulation {

using namespace project_modulation_detail;
FLASHMEM ProjectModulationResult setProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    ProjectCurveArena& arena,
    const ModulationDestination& destination,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount,
    bool enabled
) {
    if (!modulationDestinationValid(destination) ||
        spec.valueDomain != ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR ||
        spec.origin != ProjectCurveOrigin::NATIVE ||
        !validProjectCurveSpec(spec, points, pointCount) ||
        curveInputAliasesArena(arena, points, pointCount)) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT);
    }

    auto* existing = findProjectAutomationCurve(automation, destination);
    const uint8_t flags = enabled ? PROJECT_AUTOMATION_CURVE_FLAG_ENABLED : 0U;
    if (existing != nullptr) {
        const uint8_t previousFlags = existing->flags;
        const auto replaced = replaceOwnedCurve(
            arena,
            existing->curveId,
            spec,
            points,
            pointCount
        );
        if (replaced.status != ProjectModulationStatus::OK &&
            replaced.status != ProjectModulationStatus::NO_CHANGE) {
            return replaced;
        }
        existing->flags = flags;
        if (replaced.status == ProjectModulationStatus::NO_CHANGE &&
            previousFlags == flags) {
            return replaced;
        }
        return result(
            ProjectModulationStatus::OK,
            {},
            {},
            existing->curveId
        );
    }

    if (automation.entryCount >= PROJECT_AUTOMATION_ENTRY_CAPACITY) {
        return result(ProjectModulationStatus::AUTOMATION_CAPACITY_EXCEEDED);
    }
    if (arena.recordCount >= PROJECT_CURVE_LIVE_CAPACITY ||
        arena.recordCount >= PROJECT_CURVE_RECORD_CAPACITY) {
        return result(ProjectModulationStatus::CURVE_RECORD_CAPACITY_EXCEEDED);
    }
    if (pointCount > PROJECT_CURVE_POINT_CAPACITY - arena.pointCount) {
        return result(ProjectModulationStatus::CURVE_POINT_CAPACITY_EXCEEDED);
    }
    if (!canAllocateId(arena.nextCurveId)) {
        return result(ProjectModulationStatus::ID_EXHAUSTED);
    }

    const ProjectCurveId curveId = appendCurve(arena, spec, points, pointCount);
    auto& entry = automation.entries[automation.entryCount++];
    entry = {};
    entry.destination = destination;
    entry.curveId = curveId;
    entry.flags = flags;
    return result(ProjectModulationStatus::OK, {}, {}, curveId);
}

FLASHMEM ProjectModulationResult setProjectAutomationCurveSpec(
    ProjectAutomationCurveDirectory& automation,
    ProjectCurveArena& arena,
    const ModulationDestination& destination,
    const ProjectCurveSpec& spec
) {
    if (!modulationDestinationValid(destination) ||
        spec.valueDomain != ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR ||
        spec.origin != ProjectCurveOrigin::NATIVE) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT);
    }
    auto* existing = findProjectAutomationCurve(automation, destination);
    if (existing == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID);
    }
    return replaceOwnedCurveSpec(arena, existing->curveId, spec);
}

FLASHMEM ProjectModulationResult setProjectAutomationEnabled(
    ProjectAutomationCurveDirectory& automation,
    const ModulationDestination& destination,
    bool enabled
) {
    auto* entry = findProjectAutomationCurve(automation, destination);
    if (entry == nullptr) return result(ProjectModulationStatus::INVALID_ID);
    const uint8_t flags = enabled ? PROJECT_AUTOMATION_CURVE_FLAG_ENABLED : 0U;
    if (entry->flags == flags) {
        return result(ProjectModulationStatus::NO_CHANGE, {}, {}, entry->curveId);
    }
    entry->flags = flags;
    return result(ProjectModulationStatus::OK, {}, {}, entry->curveId);
}

FLASHMEM ProjectModulationResult duplicateProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    ProjectCurveArena& arena,
    const ModulationDestination& source,
    const ModulationDestination& destination
) {
    if (!modulationDestinationValid(source) ||
        !modulationDestinationValid(destination) || source == destination ||
        findProjectAutomationCurve(automation, destination) != nullptr) {
        return result(ProjectModulationStatus::INVALID_ARGUMENT);
    }
    const auto* sourceEntry = findProjectAutomationCurve(automation, source);
    if (sourceEntry == nullptr) {
        return result(ProjectModulationStatus::INVALID_ID);
    }
    auto* curve = const_cast<ProjectCurveRecord*>(findProjectCurve(
        arena,
        sourceEntry->curveId
    ));
    if (curve == nullptr || curve->referenceCount == 0U) {
        return result(ProjectModulationStatus::INVARIANT_VIOLATION);
    }
    if (automation.entryCount >= PROJECT_AUTOMATION_ENTRY_CAPACITY) {
        return result(ProjectModulationStatus::AUTOMATION_CAPACITY_EXCEEDED);
    }
    if (curve->referenceCount == std::numeric_limits<uint16_t>::max()) {
        return result(
            ProjectModulationStatus::CURVE_REFERENCE_CAPACITY_EXCEEDED
        );
    }

    auto copy = *sourceEntry;
    copy.destination = destination;
    automation.entries[automation.entryCount++] = copy;
    ++curve->referenceCount;
    return result(ProjectModulationStatus::OK, {}, {}, copy.curveId);
}

FLASHMEM ProjectModulationResult removeProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    ProjectCurveArena& arena,
    const ModulationDestination& destination
) {
    for (uint16_t index = 0; index < automation.entryCount; ++index) {
        if (automation.entries[index].destination != destination) continue;
        const ProjectCurveId curveId = automation.entries[index].curveId;
        releaseCurve(arena, curveId);
        eraseDense(automation.entries, automation.entryCount, index);
        return result(ProjectModulationStatus::OK, {}, {}, curveId);
    }
    return result(ProjectModulationStatus::INVALID_ID);
}

}  // namespace core::state::modulation

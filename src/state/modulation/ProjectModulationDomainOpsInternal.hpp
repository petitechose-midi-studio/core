#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <config/PlatformCompat.hpp>

#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation::project_modulation_detail {

inline constexpr uint8_t SOURCE_FLAGS = PROJECT_MODULATOR_FLAG_ENABLED;
inline constexpr uint8_t BINDING_FLAGS =
    PROJECT_MODULATION_BINDING_FLAG_ENABLED;
inline constexpr uint8_t TRIGGER_FLAGS =
    PROJECT_MODULATION_TRIGGER_FLAG_ENABLED;

[[nodiscard]] ProjectModulationResult result(
    ProjectModulationStatus status,
    ModulatorId sourceId = {},
    ModulationBindingId bindingId = {},
    ProjectCurveId curveId = {}
);

[[nodiscard]] bool canAllocateId(uint32_t next, uint16_t count = 1);
[[nodiscard]] uint32_t takeId(uint32_t& next);

void copyName(
    std::array<char, PROJECT_MODULATOR_NAME_CAPACITY>& destination,
    const char* requested,
    const char* fallback
);

[[nodiscard]] bool validLfoParameters(
    const ModulatorLfoParameters& parameters
);
[[nodiscard]] bool validAdsrParameters(
    const ModulatorAdsrParameters& parameters
);
[[nodiscard]] bool parameterTailZero(
    const ModulatorParameters& parameters,
    size_t first
);
[[nodiscard]] bool validTriggerFilter(
    const ModulationTriggerFilter& trigger
);
[[nodiscard]] bool validVelocityRange(uint8_t minimum, uint8_t maximum);

void eraseDense(
    std::array<ModulatorSourceState, PROJECT_MODULATOR_CAPACITY>& entries,
    uint16_t& count,
    uint16_t index
);
void eraseDense(
    std::array<
        ModulationBindingState,
        PROJECT_MODULATION_BINDING_CAPACITY
    >& entries,
    uint16_t& count,
    uint16_t index
);
void eraseDense(
    std::array<
        ModulationTriggerBindingState,
        PROJECT_MODULATION_TRIGGER_CAPACITY
    >& entries,
    uint16_t& count,
    uint16_t index
);
void eraseDense(
    std::array<
        ModulationDestinationScaleState,
        PROJECT_MODULATION_DESTINATION_SCALE_CAPACITY
    >& entries,
    uint16_t& count,
    uint16_t index
);
void eraseDense(
    std::array<
        ProjectAutomationCurveEntry,
        PROJECT_AUTOMATION_ENTRY_CAPACITY
    >& entries,
    uint16_t& count,
    uint16_t index
);

[[nodiscard]] int16_t sourceIndex(
    const ProjectModulationState& state,
    ModulatorId id
);
[[nodiscard]] int16_t outputBindingIndex(
    const ProjectModulationState& state,
    ModulationBindingId id
);
[[nodiscard]] int16_t destinationScaleIndex(
    const ProjectModulationState& state,
    const ModulationDestination& destination
);
[[nodiscard]] bool destinationHasBinding(
    const ProjectModulationState& state,
    const ModulationDestination& destination
);
void pruneDestinationScaleIfUnbound(
    ProjectModulationState& state,
    const ModulationDestination& destination
);
void pruneUnboundDestinationScales(ProjectModulationState& state);

[[nodiscard]] int16_t curveIndex(
    const ProjectCurveArena& arena,
    ProjectCurveId id
);
[[nodiscard]] int16_t triggerIndexForSource(
    const ProjectModulationState& state,
    ModulatorId sourceId
);
[[nodiscard]] int16_t triggerBindingIndex(
    const ProjectModulationState& state,
    ModulationBindingId id
);

[[nodiscard]] bool sameCurvePayload(
    const ProjectCurveArena& arena,
    const ProjectCurveRecord& record,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
);
[[nodiscard]] bool curveInputAliasesArena(
    const ProjectCurveArena& arena,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
);
void populateCurveRecord(
    ProjectCurveRecord& record,
    ProjectCurveId id,
    uint16_t pointOffset,
    uint16_t pointCount,
    uint16_t referenceCount,
    const ProjectCurveSpec& spec
);
[[nodiscard]] ProjectCurveId appendCurve(
    ProjectCurveArena& arena,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
);
void eraseCurveRecord(ProjectCurveArena& arena, uint16_t index);
void releaseCurve(ProjectCurveArena& arena, ProjectCurveId id);
[[nodiscard]] ProjectModulationResult replaceOwnedCurve(
    ProjectCurveArena& arena,
    ProjectCurveId& owner,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount,
    ModulatorId sourceId = {}
);

[[nodiscard]] bool selectedForSplit(
    ModulationBindingId id,
    const ModulatorSplitRequest& request
);
[[nodiscard]] bool validSourceName(const ModulatorSourceState& source);
[[nodiscard]] bool allZeroBytes(const uint8_t* values, size_t count);

}  // namespace core::state::modulation::project_modulation_detail

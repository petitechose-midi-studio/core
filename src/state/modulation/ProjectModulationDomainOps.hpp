#pragma once

#include <cstddef>
#include <cstdint>

#include "state/modulation/ProjectModulationState.hpp"

namespace core::state::modulation {

enum class ProjectModulationStatus : uint8_t {
    OK = 0,
    NO_CHANGE,
    INVALID_ARGUMENT,
    INVALID_ID,
    ID_EXHAUSTED,
    SOURCE_CAPACITY_EXCEEDED,
    AUTOMATION_CAPACITY_EXCEEDED,
    BINDING_CAPACITY_EXCEEDED,
    TRIGGER_CAPACITY_EXCEEDED,
    CURVE_RECORD_CAPACITY_EXCEEDED,
    CURVE_POINT_CAPACITY_EXCEEDED,
    CURVE_REFERENCE_CAPACITY_EXCEEDED,
    DUPLICATE_BINDING,
    DUPLICATE_TRIGGER,
    REACH_VIOLATION,
    INVARIANT_VIOLATION,
    HISTORY_CAPACITY_EXCEEDED,
};

struct ProjectModulationResult {
    ProjectModulationStatus status = ProjectModulationStatus::INVALID_ARGUMENT;
    ModulatorId sourceId{};
    ModulationBindingId bindingId{};
    ProjectCurveId curveId{};

    [[nodiscard]] bool changed() const {
        return status == ProjectModulationStatus::OK;
    }
};

struct ModulatorLfoDraft {
    const char* name = nullptr;
    ModulatorReach reach{};
    ModulatorLfoParameters parameters{};
    uint8_t accent = 0;
    bool enabled = true;
};

struct RecordedShapeDraft {
    const char* name = nullptr;
    ModulatorReach reach{};
    ProjectCurveSpec curve{};
    const ProjectPackedCurvePoint* points = nullptr;
    uint16_t pointCount = 0;
    uint8_t accent = 0;
    bool enabled = true;
};

struct ModulationBindingDraft {
    ModulatorId sourceId{};
    ModulationDestination destination{};
    int16_t amountQ15 = 0;
    ModulationInputRange inputRange = ModulationInputRange::BIPOLAR;
    ModulationTransfer transfer = ModulationTransfer::LINEAR;
    uint16_t slewMs = 0;
    bool enabled = true;
};

struct ModulationTriggerDraft {
    ModulatorId sourceId{};
    ModulationTriggerRef trigger{};
    bool enabled = true;
};

struct ModulatorSplitRequest {
    ModulatorId sourceId{};
    const char* cloneName = nullptr;
    ModulatorReach retainedReach{};
    ModulatorReach cloneReach{};
    const ModulationBindingId* bindingIdsToMove = nullptr;
    uint16_t bindingCountToMove = 0;
};

[[nodiscard]] bool validModulatorReach(const ModulatorReach& reach);
[[nodiscard]] bool modulatorReachContains(
    const ModulatorReach& reach,
    const ModulationDestination& destination
);
[[nodiscard]] bool validProjectCurveSpec(
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
);

[[nodiscard]] const ModulatorSourceState* findProjectModulator(
    const ProjectModulationState& state,
    ModulatorId id
);
[[nodiscard]] ModulatorSourceState* findProjectModulator(
    ProjectModulationState& state,
    ModulatorId id
);
[[nodiscard]] const ModulationBindingState* findProjectModulationBinding(
    const ProjectModulationState& state,
    ModulationBindingId id
);
[[nodiscard]] ModulationBindingState* findProjectModulationBinding(
    ProjectModulationState& state,
    ModulationBindingId id
);
void formatNextProjectLfoName(
    const ProjectModulationState& state,
    char* out,
    size_t outSize
);
void formatNextProjectModulatorName(
    const ProjectModulationState& state,
    ModulatorKind kind,
    char* out,
    size_t outSize
);
[[nodiscard]] const ProjectCurveRecord* findProjectCurve(
    const ProjectCurveArena& arena,
    ProjectCurveId id
);

[[nodiscard]] const ProjectAutomationCurveEntry* findProjectAutomationCurve(
    const ProjectAutomationCurveDirectory& automation,
    const ModulationDestination& destination
);
[[nodiscard]] ProjectAutomationCurveEntry* findProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    const ModulationDestination& destination
);

/** Adds or atomically replaces one absolute Automation curve. */
ProjectModulationResult setProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    ProjectCurveArena& arena,
    const ModulationDestination& destination,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount,
    bool enabled
);
ProjectModulationResult setProjectAutomationEnabled(
    ProjectAutomationCurveDirectory& automation,
    const ModulationDestination& destination,
    bool enabled
);
/**
 * Adds another Automation owner for the same immutable curve record.
 * The destination must not already own Automation.
 */
ProjectModulationResult duplicateProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    ProjectCurveArena& arena,
    const ModulationDestination& source,
    const ModulationDestination& destination
);
ProjectModulationResult removeProjectAutomationCurve(
    ProjectAutomationCurveDirectory& automation,
    ProjectCurveArena& arena,
    const ModulationDestination& destination
);

ProjectModulationResult createLfoModulator(
    ProjectModulationState& state,
    const ModulatorLfoDraft& draft
);
ProjectModulationResult createRecordedShapeModulator(
    ProjectModulationState& state,
    ProjectCurveArena& arena,
    const RecordedShapeDraft& draft
);
ProjectModulationResult duplicateProjectModulator(
    ProjectModulationState& state,
    ProjectCurveArena& arena,
    ModulatorId sourceId,
    const char* cloneName
);
ProjectModulationResult splitProjectModulator(
    ProjectModulationState& state,
    ProjectCurveArena& arena,
    const ModulatorSplitRequest& request
);
ProjectModulationResult deleteProjectModulator(
    ProjectModulationState& state,
    ProjectCurveArena& arena,
    ModulatorId sourceId
);
ProjectModulationResult setProjectModulatorReach(
    ProjectModulationState& state,
    ModulatorId sourceId,
    const ModulatorReach& reach
);
ProjectModulationResult setProjectModulatorEnabled(
    ProjectModulationState& state,
    ModulatorId sourceId,
    bool enabled
);
ProjectModulationResult setProjectLfoParameters(
    ProjectModulationState& state,
    ModulatorId sourceId,
    const ModulatorLfoParameters& parameters
);

ProjectModulationResult addProjectModulationBinding(
    ProjectModulationState& state,
    const ModulationBindingDraft& draft
);
ProjectModulationResult removeProjectModulationBinding(
    ProjectModulationState& state,
    ModulationBindingId bindingId
);
ProjectModulationResult updateProjectModulationBinding(
    ProjectModulationState& state,
    ModulationBindingId bindingId,
    int16_t amountQ15,
    ModulationInputRange inputRange,
    ModulationTransfer transfer,
    bool enabled,
    uint16_t slewMs = 0
);
ProjectModulationResult addProjectModulationTrigger(
    ProjectModulationState& state,
    const ModulationTriggerDraft& draft
);
ProjectModulationResult removeProjectModulationTrigger(
    ProjectModulationState& state,
    ModulationBindingId bindingId
);

/**
 * Replace one Recorded Shape payload. Shared data branches by copy-on-write;
 * unique data reuses its record and may reclaim its own point range first.
 */
ProjectModulationResult replaceRecordedShapeCurve(
    ProjectModulationState& state,
    ProjectCurveArena& arena,
    ModulatorId sourceId,
    const ProjectCurveSpec& spec,
    const ProjectPackedCurvePoint* points,
    uint16_t pointCount
);

/** Cold, exhaustive invariant check used at persistence and test boundaries. */
[[nodiscard]] bool validProjectModulationDomain(
    const ProjectModulationState& state,
    const ProjectCurveArena& arena,
    const ProjectAutomationCurveDirectory* automation = nullptr
);

}  // namespace core::state::modulation

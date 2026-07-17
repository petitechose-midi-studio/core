#pragma once

#include <array>
#include <cstdint>
#include <limits>
#include <type_traits>

#include "state/modulation/ProjectControlDomainState.hpp"
#include "state/modulation/ProjectModulationDomainOps.hpp"

namespace core::state::modulation {

struct ProjectModulationCompileContext {
    uint16_t enabledTrackMask = 0xFFFFU;
    std::array<uint8_t, PROJECT_MODULATION_TRACK_COUNT> activePage{};
    std::array<uint8_t, PROJECT_MODULATION_TRACK_COUNT> activeMacroMask{};
};

struct ProjectModulationRuntimeSource {
    ModulatorId id{};
    uint32_t periodTicks = 0;
    uint32_t freePeriodMs = 0;
    int16_t phaseQ15 = 0;
    uint16_t curveRecordIndex = std::numeric_limits<uint16_t>::max();
    ModulationTriggerRef trigger{};
    ModulatorKind kind = ModulatorKind::LFO;
    uint8_t flags = 0;
    ModulatorLfoShape shape = ModulatorLfoShape::SINE;
    ModulatorRetriggerPolicy retrigger = ModulatorRetriggerPolicy::FREE_RUNNING;
    ModulatorTimingMode timing = ModulatorTimingMode::SYNC;
    uint8_t triggerFlags = 0;
    uint16_t reserved = 0;
};

struct ProjectModulationRuntimeBinding {
    ModulationBindingId id{};
    uint16_t sourceIndex = 0;
    uint16_t destinationIndex = 0;
    int16_t amountQ15 = 0;
    uint16_t slewMs = 0;
    ResolvedModulationMapping mapping = ResolvedModulationMapping::IDENTITY;
    ModulationTransfer transfer = ModulationTransfer::LINEAR;
    uint8_t flags = 0;
    uint8_t reserved = 0;
};

inline constexpr uint8_t PROJECT_CONTROL_RUNTIME_DESTINATION_FLAG_AUTOMATION_ENABLED =
    0x01U;

struct ProjectModulationRuntimeDestination {
    ModulationDestination destination{};
    uint16_t firstBinding = 0;
    uint16_t bindingCount = 0;
    uint16_t stableAddress = 0;
    uint16_t automationCurveRecordIndex = std::numeric_limits<uint16_t>::max();
    float minimum = 0.0f;
    float maximum = 1.0f;
    uint16_t destinationScaleQ15 =
        PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    uint8_t flags = 0;
    uint8_t reserved = 0;
};

/**
 * Hot, pointer-free projection. Every active assignment is compiled; no
 * hidden voice cap or silent truncation is permitted.
 */
struct ProjectModulationRuntimePlan {
    uint16_t sourceCount = 0;
    uint16_t bindingCount = 0;
    uint16_t destinationCount = 0;
    uint16_t inactiveBindingCount = 0;
    uint16_t automationCount = 0;
    uint16_t inactiveAutomationCount = 0;
    uint32_t contextHash = 0;
    std::array<
        ProjectModulationRuntimeSource,
        PROJECT_MODULATOR_CAPACITY
    > sources{};
    std::array<
        ProjectModulationRuntimeBinding,
        PROJECT_MODULATION_BINDING_CAPACITY
    > bindings{};
    std::array<
        ProjectModulationRuntimeDestination,
        PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY
    > destinations{};
    std::array<uint16_t, PROJECT_MODULATION_BINDING_CAPACITY> bindingOrder{};
};

enum class ProjectModulationCompileStatus : uint8_t {
    OK = 0,
    INVALID_CONTEXT,
    INVALID_DOMAIN,
    CAPACITY_EXCEEDED,
};

struct ProjectModulationCompileResult {
    ProjectModulationCompileStatus status =
        ProjectModulationCompileStatus::INVALID_DOMAIN;
    uint16_t sourceCount = 0;
    uint16_t bindingCount = 0;
    uint16_t destinationCount = 0;
    uint16_t inactiveBindingCount = 0;
    uint16_t automationCount = 0;
    uint16_t inactiveAutomationCount = 0;

    [[nodiscard]] bool compiled() const {
        return status == ProjectModulationCompileStatus::OK;
    }
};

struct ProjectModulationResolveResult {
    float value = 0.0f;
    float modulation = 0.0f;
    uint16_t contributionCount = 0;
    bool clipped = false;
    bool valid = false;
};

[[nodiscard]] bool validProjectModulationCompileContext(
    const ProjectModulationCompileContext& context
);
[[nodiscard]] uint32_t projectModulationCompileContextHash(
    const ProjectModulationCompileContext& context
);

/** Atomic publication: `out` is untouched whenever compilation fails. */
ProjectModulationCompileResult compileProjectModulationRuntimePlan(
    const ProjectModulationState& state,
    const ProjectCurveArena& arena,
    const ProjectModulationCompileContext& context,
    ProjectModulationRuntimePlan& out
);

/** Compiles the union of absolute Automation and relative Modulation targets. */
ProjectModulationCompileResult compileProjectControlRuntimePlan(
    const ProjectControlDomainState& state,
    const ProjectModulationCompileContext& context,
    ProjectModulationRuntimePlan& out
);

/** Pure deterministic sum followed by one final destination clamp. */
ProjectModulationResolveResult resolveProjectModulationDestination(
    const ProjectModulationRuntimePlan& plan,
    uint16_t destinationIndex,
    const float* naturalSourceValues,
    float baseValue
);

static_assert(sizeof(ProjectModulationRuntimeSource) == 28U);
static_assert(sizeof(ProjectModulationRuntimeBinding) == 16U);
static_assert(sizeof(ProjectModulationRuntimeDestination) == 24U);
static_assert(sizeof(ProjectModulationRuntimePlan) == 15888U);
static_assert(std::is_trivially_copyable_v<ProjectModulationRuntimePlan>);

}  // namespace core::state::modulation

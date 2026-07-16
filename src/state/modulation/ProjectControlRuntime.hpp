#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/modulation/ProjectModulationRuntimePlan.hpp"

namespace core::state::modulation {

inline constexpr uint16_t PROJECT_CONTROL_TICKS_PER_BEAT = 192;

/**
 * Immutable time captured once by the Project runtime owner for one control
 * frame. Sync sources use musicalTick; Free sources use monotonicMs.
 */
struct ProjectControlTimeSnapshot {
    uint32_t musicalTick = 0;
    uint32_t monotonicMs = 0;
    uint32_t transportGeneration = 0;
    uint32_t transportStartMusicalTick = 0;
    uint32_t transportStartMonotonicMs = 0;
    uint16_t musicalTickFractionQ16 = 0;
    bool playing = false;
    uint8_t reserved = 0;
};

struct ProjectModulationTriggerFrame {
    uint16_t count = 0;
    uint16_t reserved = 0;
    std::array<
        ModulationTriggerRef,
        PROJECT_MODULATION_TRIGGER_CAPACITY
    > events{};
};

struct ProjectLogicalMacroBaseInput {
    float staticValue = 0.0f;
    float manualValue = 0.0f;
    bool manualOverride = false;
    std::array<uint8_t, 3> reserved{};
};

inline constexpr uint8_t PROJECT_LOGICAL_MACRO_FLAG_AUTOMATION_ACTIVE = 0x01U;
inline constexpr uint8_t PROJECT_LOGICAL_MACRO_FLAG_MANUAL_OVERRIDE = 0x02U;
inline constexpr uint8_t PROJECT_LOGICAL_MACRO_FLAG_MODULATION_ACTIVE = 0x04U;
inline constexpr uint8_t PROJECT_LOGICAL_MACRO_FLAG_CLIPPED = 0x08U;

struct ProjectLogicalMacroRuntimeValue {
    ModulationDestination destination{};
    float base = 0.0f;
    float modulation = 0.0f;
    float value = 0.0f;
    uint16_t contributionCount = 0;
    uint8_t flags = 0;
    uint8_t reserved = 0;
};

struct ProjectControlRuntimeFrame {
    uint32_t sequence = 0;
    uint16_t sourceCount = 0;
    uint16_t destinationCount = 0;
    std::array<float, PROJECT_MODULATOR_CAPACITY> sourceValues{};
    std::array<
        ProjectLogicalMacroRuntimeValue,
        PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY
    > destinations{};
};

struct ProjectModulationRuntimeSourceState {
    ModulatorId id{};
    uint32_t explicitMusicalAnchorTick = 0;
    uint32_t explicitMonotonicAnchorMs = 0;
    uint16_t explicitMusicalAnchorFractionQ16 = 0;
    bool explicitlyTriggered = false;
    uint8_t reserved = 0;
};

/**
 * Runtime-only phase and slew memory. IDs let a cold graph publication retain
 * unaffected phase/slew state without heap allocation or array-position
 * identity. Q15 contributions keep the maximum footprint bounded.
 */
struct ProjectControlRuntimeState {
    uint32_t activationMusicalTick = 0;
    uint32_t activationMonotonicMs = 0;
    uint32_t lastEvaluationMs = 0;
    uint32_t frameSequence = 0;
    uint16_t activationMusicalTickFractionQ16 = 0;
    uint16_t sourceCount = 0;
    uint16_t bindingCount = 0;
    bool initialized = false;
    uint8_t reserved = 0;
    std::array<
        ProjectModulationRuntimeSourceState,
        PROJECT_MODULATOR_CAPACITY
    > sources{};
    std::array<
        ModulationBindingId,
        PROJECT_MODULATION_BINDING_CAPACITY
    > bindingIds{};
    std::array<int16_t, PROJECT_MODULATION_BINDING_CAPACITY>
        bindingContributionQ15{};
};

enum class ProjectControlRuntimeStatus : uint8_t {
    OK = 0,
    INVALID_ARGUMENT,
    INVALID_PLAN,
    INVALID_TIME,
    STATE_NOT_SYNCHRONIZED,
};

struct ProjectControlRuntimeResult {
    ProjectControlRuntimeStatus status =
        ProjectControlRuntimeStatus::INVALID_ARGUMENT;
    uint16_t sourceEvaluationCount = 0;
    uint16_t destinationEvaluationCount = 0;
    uint16_t contributionCount = 0;
    uint16_t clippedDestinationCount = 0;

    [[nodiscard]] bool evaluated() const {
        return status == ProjectControlRuntimeStatus::OK;
    }
};

/** Project activation boundary. */
void resetProjectControlRuntimeState(
    ProjectControlRuntimeState& state,
    const ProjectControlTimeSnapshot& time
);

/**
 * Cold plan-publication boundary. Surviving source and binding IDs retain
 * their runtime facts; new connections start with zero slew contribution.
 */
ProjectControlRuntimeStatus synchronizeProjectControlRuntimeState(
    ProjectControlRuntimeState& state,
    const ProjectModulationRuntimePlan& plan,
    const ProjectControlTimeSnapshot& time
);

/** Canonical bipolar source shape at normalized phase [0, 1). */
float evaluateProjectLfoShape(ModulatorLfoShape shape, float phase);

using ProjectLogicalMacroRuntimeSink = void (*)(
    void* context,
    uint16_t destinationIndex,
    const ProjectLogicalMacroRuntimeValue& value
);

/**
 * Memory-minimal evaluator used by the product runtime. The caller supplies
 * only the 128-float source scratch and an unpublished destination sink; this
 * avoids retaining the 3080-byte diagnostic frame in firmware.
 */
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
);

/**
 * Diagnostic/test convenience wrapper. Product integration should use the
 * memory-minimal sink API above rather than retain this complete frame.
 */
ProjectControlRuntimeResult evaluateProjectControlRuntimeFrame(
    const ProjectModulationRuntimePlan& plan,
    const ProjectCurveArena& arena,
    const ProjectControlTimeSnapshot& time,
    const ProjectModulationTriggerFrame& triggers,
    const ProjectLogicalMacroBaseInput* bases,
    uint16_t baseCount,
    ProjectControlRuntimeState& state,
    ProjectControlRuntimeFrame& out
);

static_assert(sizeof(ProjectControlTimeSnapshot) == 24U);
static_assert(sizeof(ProjectLogicalMacroBaseInput) == 12U);
static_assert(sizeof(ProjectLogicalMacroRuntimeValue) == 20U);
static_assert(sizeof(ProjectModulationRuntimeSourceState) == 16U);
static_assert(sizeof(ProjectControlRuntimeState) == 5144U);
static_assert(std::is_trivially_copyable_v<ProjectControlRuntimeState>);
static_assert(std::is_trivially_copyable_v<ProjectControlRuntimeFrame>);

}  // namespace core::state::modulation

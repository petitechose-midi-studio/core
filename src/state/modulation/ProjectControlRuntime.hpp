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

/** Two bounded clock observations used only for smooth UI extrapolation. */
struct ProjectControlTimeTelemetry {
    ProjectControlTimeSnapshot previous{};
    ProjectControlTimeSnapshot current{};
    uint32_t revision = 0U;
};

/** Edge observed at the authoritative dispatch boundary. */
enum class ProjectModulationTriggerEdge : uint8_t {
    PULSE = 0,
    GATE_ON,
    GATE_OFF,
};

/**
 * Compact runtime event. Velocity is observed for future generators but V1
 * ADSR amplitude deliberately remains independent from it.
 */
struct ProjectModulationTriggerEvent {
    ModulationTriggerRef trigger{};
    ProjectModulationTriggerEdge edge = ProjectModulationTriggerEdge::PULSE;
    uint8_t velocity = 0U;
};

inline constexpr uint16_t PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY = 256U;

struct ProjectModulationTriggerFrame {
    uint16_t count = 0;
    /** Lost SPSC edges since the previous drain; forces a safe gate release. */
    uint16_t droppedEventCount = 0;
    std::array<
        ProjectModulationTriggerEvent,
        PROJECT_MODULATION_TRIGGER_EVENT_CAPACITY
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
    /** Automation/static base evaluated at the current phase, before Manual. */
    float underlyingBase = 0.0f;
    /** Effective base: underlyingBase or the current Manual override. */
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

struct ProjectModulationRuntimeLfoState {
    ModulatorKind kind = ModulatorKind::LFO;
    bool explicitlyTriggered = false;
    uint16_t explicitMusicalAnchorFractionQ16 = 0;
    uint32_t explicitMusicalAnchorTick = 0;
    uint32_t explicitMonotonicAnchorMs = 0;
};

/**
 * Hot recorded-shape segment. The packed endpoints avoid repeated random
 * ProjectCurveArena reads while keeping the per-source state footprint fixed.
 */
struct ProjectModulationRuntimeRecordedCurveState {
    ModulatorKind kind = ModulatorKind::RECORDED_SHAPE;
    bool segmentValid = false;
    uint16_t segmentHint = 1;
    uint16_t leftTick = 0;
    uint16_t rightTick = 0;
    int16_t leftValue = 0;
    int16_t rightValue = 0;
};

enum class ProjectModulationAdsrStage : uint8_t {
    IDLE = 0,
    DELAY,
    ATTACK,
    HOLD,
    DECAY,
    SUSTAIN,
    RELEASE,
};

struct ProjectModulationRuntimeAdsrState {
    ModulatorKind kind = ModulatorKind::ADSR;
    ProjectModulationAdsrStage stage = ProjectModulationAdsrStage::IDLE;
    uint8_t heldNoteCount = 0U;
    uint8_t flags = 0U;
    uint32_t stageAnchor = 0U;
    uint16_t stageAnchorFractionQ16 = 0U;
    int16_t stageStartLevelQ15 = 0;
    int16_t smoothedLevelQ15 = 0;
    uint16_t routeSignature = 0U;
    std::array<uint32_t, 4> acceptedNotes{};
};

union ProjectModulationRuntimeSourcePayload {
    ProjectModulationRuntimeLfoState lfo{};
    ProjectModulationRuntimeRecordedCurveState recordedCurve;
    ProjectModulationRuntimeAdsrState adsr;
};

struct ProjectModulationRuntimeSourceState {
    ModulatorId id{};
    ProjectModulationRuntimeSourcePayload payload{};
};

enum class ProjectRecordedShapeRuntimeAuditionMode : uint8_t {
    NONE = 0,
    SOURCE_OVERRIDE,
    DESTINATION_ADD,
};

/**
 * One session-only Recorded Shape audition projected by the hot resolver.
 *
 * The capture grid remains in lazy PSRAM UI state. The runtime retains only
 * its current signed sample and routing identity, so live overdub never copies
 * or recompiles a curve per encoder event.
 */
struct ProjectRecordedShapeRuntimeAudition {
    ModulatorId sourceId{};
    ModulationDestination destination{};
    int16_t sourceValueQ15 = 0;
    int16_t amountQ15 = 0;
    uint16_t destinationScaleQ15 =
        PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15;
    ProjectRecordedShapeRuntimeAuditionMode mode =
        ProjectRecordedShapeRuntimeAuditionMode::NONE;
    uint8_t reserved = 0U;

    [[nodiscard]] bool active() const {
        return mode != ProjectRecordedShapeRuntimeAuditionMode::NONE;
    }
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
    uint32_t lastEvaluationMusicalTick = 0;
    uint32_t frameSequence = 0;
    uint16_t activationMusicalTickFractionQ16 = 0;
    uint16_t lastEvaluationMusicalTickFractionQ16 = 0;
    uint16_t sourceCount = 0;
    uint16_t bindingCount = 0;
    bool initialized = false;
    uint8_t reserved = 0;
    ProjectRecordedShapeRuntimeAudition recordedShapeAudition{};
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

struct ProjectModulatorRuntimeProjection {
    float value = 0.0f;
    float rawValue = 0.0f;
    uint16_t positionQ16 = 0U;
    uint16_t stageProgressQ16 = 0U;
    ProjectModulationAdsrStage adsrStage = ProjectModulationAdsrStage::IDLE;
    ModulatorKind kind = ModulatorKind::LFO;
    bool positionKnown = false;
    uint8_t reserved = 0U;
};

void publishProjectControlTimeTelemetry(
    ProjectControlTimeTelemetry& telemetry,
    const ProjectControlTimeSnapshot& time
);

[[nodiscard]] ProjectControlTimeSnapshot extrapolateProjectControlTime(
    const ProjectControlTimeTelemetry& telemetry,
    uint32_t nowMs
);

[[nodiscard]] uint16_t projectControlTimelinePositionQ16(
    const ProjectControlRuntimeState& state,
    const ProjectControlTimeSnapshot& time,
    uint16_t durationTicks
);

/**
 * Maps the phase-shifted LFO runtime position back onto the unshifted preview
 * timeline. Curve sampling applies authored phase itself; markers must use
 * this coordinate exactly once to stay aligned with the rendered waveform.
 */
[[nodiscard]] uint16_t projectLfoPreviewPositionQ16(
    uint16_t runtimePositionQ16,
    int16_t authoredPhaseQ15
);

/** Applies authored phase once to a phase-free preview coordinate. */
[[nodiscard]] uint16_t projectLfoShapePositionQ16(
    uint16_t previewPositionQ16,
    int16_t authoredPhaseQ15
);

/** Cold/UI projection using the exact runtime source evaluator semantics. */
[[nodiscard]] bool projectModulatorRuntimeProjectionAtIndex(
    const ProjectModulationRuntimePlan& plan,
    const ProjectCurveArena& arena,
    const ProjectControlRuntimeState& state,
    const ProjectControlTimeSnapshot& time,
    uint16_t sourceIndex,
    ProjectModulatorRuntimeProjection& out
);

/** Convenience lookup for cold callers that do not retain a compiled index. */
[[nodiscard]] bool projectModulatorRuntimeProjection(
    const ProjectModulationRuntimePlan& plan,
    const ProjectCurveArena& arena,
    const ProjectControlRuntimeState& state,
    const ProjectControlTimeSnapshot& time,
    ModulatorId sourceId,
    ProjectModulatorRuntimeProjection& out
);

/** Project activation boundary. */
void resetProjectControlRuntimeState(
    ProjectControlRuntimeState& state,
    const ProjectControlTimeSnapshot& time
);

/** Publishes the current take sample over one already-compiled Source. */
[[nodiscard]] bool setProjectRecordedShapeSourceAudition(
    ProjectControlRuntimeState& state,
    ModulatorId sourceId,
    int16_t sourceValueQ15
);

/** Adds one not-yet-committed edge to an already-compiled destination. */
[[nodiscard]] bool setProjectRecordedShapeDestinationAudition(
    ProjectControlRuntimeState& state,
    const ModulationDestination& destination,
    int16_t amountQ15,
    int16_t sourceValueQ15,
    uint16_t destinationScaleQ15 =
        PROJECT_MODULATION_DESTINATION_SCALE_ONE_Q15
);

void clearProjectRecordedShapeRuntimeAudition(
    ProjectControlRuntimeState& state
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

/** Canonical ADSR stage progress at normalized position [0, 1]. */
float evaluateProjectAdsrProgress(ModulatorAdsrCurve curve, float progress);

using ProjectLogicalMacroRuntimeSink = void (*)(
    void* context,
    uint16_t destinationIndex,
    const ProjectLogicalMacroRuntimeValue& value
);

using ProjectLogicalMacroBaseProvider = bool (*)(
    void* context,
    uint16_t destinationIndex,
    const ModulationDestination& destination,
    ProjectLogicalMacroBaseInput& out
);

/** Product path: resolves one base lazily, with no 128-entry base array. */
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
static_assert(sizeof(ProjectControlTimeTelemetry) == 52U);
static_assert(sizeof(ProjectModulationTriggerEvent) == 6U);
static_assert(sizeof(ProjectModulationTriggerFrame) == 1540U);
static_assert(sizeof(ProjectLogicalMacroBaseInput) == 12U);
static_assert(sizeof(ProjectLogicalMacroRuntimeValue) == 24U);
static_assert(sizeof(ProjectModulationRuntimeLfoState) == 12U);
static_assert(sizeof(ProjectModulationRuntimeRecordedCurveState) == 12U);
static_assert(sizeof(ProjectModulationRuntimeAdsrState) == 32U);
static_assert(sizeof(ProjectModulationRuntimeSourcePayload) == 32U);
static_assert(sizeof(ProjectModulationRuntimeSourceState) == 36U);
static_assert(sizeof(ProjectRecordedShapeRuntimeAudition) <= 20U);
static_assert(sizeof(ProjectControlRuntimeState) == 7728U);
static_assert(std::is_trivially_copyable_v<ProjectControlRuntimeState>);
static_assert(std::is_trivially_copyable_v<ProjectControlRuntimeFrame>);
static_assert(std::is_trivially_copyable_v<ProjectModulationTriggerFrame>);

}  // namespace core::state::modulation

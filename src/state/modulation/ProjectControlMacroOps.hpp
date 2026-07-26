#pragma once

#include <cstdint>

#include "state/macro/MacroAutomationAddress.hpp"
#include "state/macro/MacroAutomationDomain.hpp"
#include "state/modulation/ProjectControlState.hpp"

namespace core::state::modulation {

struct ProjectControlCurveView {
    ProjectCurveId id{};
    ProjectCurveSpec spec{};
    uint16_t pointOffset = 0;
    uint16_t pointCount = 0;
    bool enabled = false;

    [[nodiscard]] bool stored() const {
        return valid(id) && pointCount > 0U;
    }
};

struct ProjectControlModulationView {
    ModulatorId sourceId{};
    ModulationBindingId bindingId{};
    ProjectControlCurveView recordedShape{};
    float amount = 0.0f;
    bool enabled = false;

    [[nodiscard]] bool present() const {
        return valid(sourceId) && valid(bindingId);
    }

    [[nodiscard]] bool isRecordedShape() const {
        return recordedShape.stored();
    }
};

/** Read-only graph projection for one logical Macro destination. */
struct ProjectControlMacroDestinationView {
    macro::MacroAutomationSlotAddress address{};
    ProjectControlCurveView automation{};
    ProjectControlModulationView primaryModulation{};
    uint16_t modulationCount = 0;
    uint16_t activeModulationCount = 0;

    [[nodiscard]] bool present() const {
        return automation.stored() || modulationCount > 0U;
    }

    [[nodiscard]] bool mutationAmbiguous() const {
        return modulationCount > 1U;
    }
};

/** Detached graph-native curve used by clipboard and History transactions. */
struct ProjectControlCurvePayload {
    ProjectCurveSpec spec{};
    uint16_t pointOffset = 0;
    uint16_t pointCount = 0;
    bool enabled = false;

    [[nodiscard]] bool stored() const {
        return pointCount > 0U;
    }
};

/** Detached Automation plus one Recorded Shape assignment. */
struct ProjectControlMacroDestinationPayload {
    ProjectControlCurvePayload automation{};
    ProjectControlCurvePayload recordedShape{};
    float modulationAmount = 0.0f;

    [[nodiscard]] bool present() const {
        return automation.stored() || recordedShape.stored();
    }
};

enum class ProjectAutomationConversionPolicy : uint8_t {
    MEAN = 0,
    FIRST,
    MIN,
};

enum class ProjectAutomationConversionStatus : uint8_t {
    READY = 0,
    OVERWRITE_REQUIRED,
    INVALID_ADDRESS,
    INVALID_DOMAIN,
    NO_AUTOMATION,
    SOURCE_CAPACITY_EXHAUSTED,
    BINDING_CAPACITY_EXHAUSTED,
    CURVE_CAPACITY_EXHAUSTED,
    POINT_CAPACITY_EXHAUSTED,
    STALE_PLAN,
};

struct ProjectAutomationConversionPlan {
    ProjectAutomationConversionStatus status =
        ProjectAutomationConversionStatus::INVALID_ADDRESS;
    macro::MacroAutomationSlotAddress address{};
    ProjectAutomationConversionPolicy policy =
        ProjectAutomationConversionPolicy::MEAN;
    float reference = 0.0f;
    float normalizationAmplitude = 0.0f;
    float expectedStaticBase = 0.0f;
    uint16_t pointCount = 0;
    uint16_t existingBindingCount = 0;
    uint16_t freePointCount = 0;
    uint32_t sourceFingerprint = 0;
    uint32_t targetFingerprint = 0;
    bool overwritesModulation = false;

    [[nodiscard]] bool actionable() const {
        return status == ProjectAutomationConversionStatus::READY ||
               status ==
                   ProjectAutomationConversionStatus::OVERWRITE_REQUIRED;
    }
};

struct ProjectControlCurvePoint {
    float beat = 0.0f;
    float value = 0.0f;
};

struct ProjectControlCurveWindowSummary {
    bool active = false;
    uint16_t sourceDurationTicks = 0;
    uint16_t durationTicks = 0;
    uint16_t windowOffsetTicks = 0;
    uint16_t firstPointTick = 0;
    uint16_t lastPointTick = 0;
    uint16_t pointCount = 0;
    bool wraps = false;
};

[[nodiscard]] ModulationDestination projectControlDestination(
    const macro::MacroAutomationSlotAddress& address
);

[[nodiscard]] bool readProjectControlMacroDestination(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ProjectControlMacroDestinationView& out
);

/** Resolves and remembers one stable assignment focus for a logical Macro. */
[[nodiscard]] ModulationBindingId projectControlFocusedModulationBinding(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
);

[[nodiscard]] bool setProjectControlFocusedModulationBinding(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ModulationBindingId bindingId
);

[[nodiscard]] bool setProjectControlAutomationEnabled(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    bool enabled
);
[[nodiscard]] bool setProjectControlModulationEnabled(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    bool enabled
);
[[nodiscard]] bool setProjectControlModulationAmount(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    float amount
);
[[nodiscard]] bool clearProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
);
[[nodiscard]] bool clearProjectControlModulation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address
);

/** Removes non-retained Pages and compacts every retained destination. */
[[nodiscard]] bool compactProjectControlPages(
    ProjectControlState& control,
    uint8_t track,
    uint16_t retainedPageMask
);

/** Caller-owned cold-domain variant used by atomic structural transactions. */
[[nodiscard]] bool compactProjectControlPagesInDomain(
    ProjectControlDomainState& domain,
    uint8_t track,
    uint16_t retainedPageMask
);

[[nodiscard]] bool assignProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationLane& lane
);

/**
 * Captures one graph-native destination into caller-owned cold storage.
 * Points are packed Automation first, then the primary Recorded Shape.
 */
[[nodiscard]] bool captureProjectControlMacroDestination(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ProjectControlMacroDestinationPayload& outState,
    ProjectPackedCurvePoint* outPoints,
    uint16_t pointCapacity,
    uint16_t& automationPointCount,
    uint16_t& modulationPointCount
);

/**
 * Atomically replaces one slot-compatible destination while preserving all
 * unrelated project-root sources and assignments. Ambiguous multi-source
 * destinations are rejected for the later graph-aware structural workflow.
 */
[[nodiscard]] bool replaceProjectControlMacroDestination(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlMacroDestinationPayload& sourceState,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
);

/**
 * Domain-level variant for a caller-owned cold transaction. The caller must
 * validate and publish the complete domain; this function never touches live
 * revisions or derived runtime state.
 */
[[nodiscard]] bool replaceProjectControlMacroDestinationInDomain(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlMacroDestinationPayload& sourceState,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
);

/** Replaces only absolute Automation and preserves every Modulation edge. */
[[nodiscard]] bool replaceProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlCurvePayload& source,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
);

/**
 * Cold transaction variant. It mutates only the caller-owned authored domain
 * and never publishes a revision or touches derived runtime state.
 */
[[nodiscard]] bool replaceProjectControlAutomationInDomain(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlCurvePayload& source,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
);

/** Replaces only the primary Recorded Shape assignment. */
[[nodiscard]] bool replaceProjectControlRecordedShape(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const ProjectControlCurvePayload& source,
    float amount,
    const ProjectPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
);

[[nodiscard]] ProjectAutomationConversionPlan
preflightProjectControlConversion(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ProjectAutomationConversionPolicy policy,
    float currentStaticBase
);

[[nodiscard]] bool applyProjectControlConversion(
    ProjectControlState& control,
    float& staticBase,
    const ProjectAutomationConversionPlan& plan,
    bool overwriteConfirmed
);

[[nodiscard]] bool setProjectControlAutomationDurationBeats(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    float durationBeats
);

[[nodiscard]] bool setProjectControlAutomationWindowOffsetBeats(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    float offsetBeats
);

[[nodiscard]] bool readProjectControlCurvePoint(
    const ProjectControlState& control,
    ProjectCurveId curveId,
    uint16_t pointIndex,
    bool signedOutput,
    ProjectControlCurvePoint& out
);

[[nodiscard]] ProjectControlCurveWindowSummary
projectControlCurveWindowSummary(
    const ProjectControlState& control,
    ProjectCurveId curveId
);

/** Cold/UI sampling helper. Product playback uses the precompiled evaluator. */
[[nodiscard]] float evaluateProjectControlCurve(
    const ProjectControlState& control,
    ProjectCurveId curveId,
    float elapsedBeat,
    float fallback
);

}  // namespace core::state::modulation

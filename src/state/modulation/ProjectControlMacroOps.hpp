#pragma once

#include <cstdint>

#include "state/macro/MacroAutomationState.hpp"
#include "state/modulation/ProjectControlState.hpp"

namespace core::state::modulation {

struct ProjectControlMacroSlotView {
    macro::MacroAutomationSlotAddress address{};
    macro::MacroAutomationSlotState legacy{};
    ProjectCurveId automationCurveId{};
    ModulatorId modulationSourceId{};
    ModulationBindingId modulationBindingId{};
    ProjectCurveId modulationCurveId{};
    uint16_t modulationCount = 0;
    uint16_t activeModulationCount = 0;
    bool present = false;
    bool automationStored = false;
    bool automationEnabled = false;
    bool modulationStored = false;
    bool modulationEnabled = false;
    bool primaryRecordedShape = false;
    bool legacyMutationAmbiguous = false;
};

[[nodiscard]] ModulationDestination projectControlDestination(
    const macro::MacroAutomationSlotAddress& address
);

[[nodiscard]] bool readProjectControlMacroSlot(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    ProjectControlMacroSlotView& out
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

[[nodiscard]] bool assignProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationLane& lane
);

/**
 * Captures one legacy-compatible destination into caller-owned cold storage.
 * Points are packed Automation first, then the primary Recorded Shape.
 */
[[nodiscard]] bool captureProjectControlMacroSlot(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    macro::MacroAutomationSlotState& outState,
    macro::MacroPackedCurvePoint* outPoints,
    uint16_t pointCapacity,
    uint16_t& automationPointCount,
    uint16_t& modulationPointCount
);

/**
 * Atomically replaces one legacy-compatible destination while preserving all
 * unrelated project-root sources and assignments. Ambiguous multi-source
 * destinations are rejected for the later graph-aware structural workflow.
 */
[[nodiscard]] bool replaceProjectControlMacroSlot(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationSlotState& sourceState,
    const macro::MacroPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
);

/**
 * Domain-level variant for a caller-owned cold transaction. The caller must
 * validate and publish the complete domain; this function never touches live
 * revisions or derived runtime state.
 */
[[nodiscard]] bool replaceProjectControlMacroSlotInDomain(
    ProjectControlDomainState& domain,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationSlotState& sourceState,
    const macro::MacroPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
);

/** Replaces only absolute Automation and preserves every Modulation edge. */
[[nodiscard]] bool replaceProjectControlAutomation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationCurveRef& source,
    const macro::MacroPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
);

/** Replaces only the legacy-compatible primary Recorded Shape assignment. */
[[nodiscard]] bool replaceProjectControlModulation(
    ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    const macro::MacroAutomationCurveRef& source,
    float amount,
    const macro::MacroPackedCurvePoint* sourcePoints,
    uint16_t sourcePointCount
);

[[nodiscard]] macro::MacroAutomationConversionPlan
preflightProjectControlConversion(
    const ProjectControlState& control,
    const macro::MacroAutomationSlotAddress& address,
    macro::MacroAutomationConversionPolicy policy,
    float currentStaticBase
);

[[nodiscard]] bool applyProjectControlConversion(
    ProjectControlState& control,
    float& staticBase,
    const macro::MacroAutomationConversionPlan& plan,
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
    macro::MacroCurvePoint& out
);

[[nodiscard]] macro::MacroAutomationCurveWindowSummary
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

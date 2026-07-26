#pragma once

#include <cstdint>
#include <limits>

#include "state/modulation/ProjectControlRuntime.hpp"

namespace core::state::modulation::project_control_runtime_detail {

inline constexpr float Q15_SCALE = 32767.0f;
inline constexpr float Q16_SCALE = 65536.0f;
inline constexpr float ADSR_SUSTAIN_SCALE = 32768.0f;
inline constexpr uint16_t INVALID_CURVE_RECORD =
    std::numeric_limits<uint16_t>::max();

[[nodiscard]] bool validTime(const ProjectControlTimeSnapshot& time);
[[nodiscard]] bool validPlanBounds(
    const ProjectModulationRuntimePlan& plan
);
void synchronizeSources(
    ProjectControlRuntimeState& state,
    const ProjectModulationRuntimePlan& plan,
    const ProjectControlTimeSnapshot& time
);
void synchronizeBindings(
    ProjectControlRuntimeState& state,
    const ProjectModulationRuntimePlan& plan
);

[[nodiscard]] float wrapPhase(float phase);
[[nodiscard]] float phaseFromMusicalTime(
    const ProjectControlTimeSnapshot& time,
    uint32_t anchorTick,
    uint16_t anchorFractionQ16,
    uint32_t periodTicks,
    int16_t phaseQ15
);
void elapsedMusicalTime(
    const ProjectControlTimeSnapshot& time,
    uint32_t anchorTick,
    uint16_t anchorFractionQ16,
    uint32_t& elapsedTick,
    uint16_t& elapsedFractionQ16
);
[[nodiscard]] float phaseFromFreeTime(
    uint32_t nowMs,
    uint32_t anchorMs,
    uint32_t periodMs,
    int16_t phaseQ15
);
[[nodiscard]] float evaluateProjectCurve(
    const ProjectCurveArena& arena,
    const ProjectModulationRuntimeCurve& curve,
    uint32_t elapsedTick,
    uint16_t elapsedFractionQ16,
    float fallback,
    ProjectModulationRuntimeRecordedCurveState* cache
);
[[nodiscard]] float evaluateProjectCurve(
    const ProjectCurveArena& arena,
    uint16_t recordIndex,
    uint32_t elapsedTick,
    uint16_t elapsedFractionQ16,
    float fallback
);

[[nodiscard]] int16_t packQ15(float value);
[[nodiscard]] float unpackQ15(int16_t value);
[[nodiscard]] bool adsrStageCompleteAndProgress(
    const ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time,
    ModulatorTimingMode timing,
    uint32_t duration,
    float& progress
);
[[nodiscard]] uint32_t adsrStageDuration(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationAdsrStage stage
);
[[nodiscard]] float advanceAdsrRawToTime(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time
);
[[nodiscard]] int16_t advanceAdsrSmoothQ15(
    const ProjectModulationRuntimeSource& source,
    ProjectModulationRuntimeAdsrState& state,
    const ProjectControlTimeSnapshot& time,
    uint32_t previousMs,
    uint32_t previousMusicalTick,
    uint16_t previousMusicalFractionQ16,
    float rawValue
);
[[nodiscard]] bool routeProjectTriggerFrame(
    const ProjectModulationRuntimePlan& plan,
    const ProjectControlTimeSnapshot& time,
    const ProjectModulationTriggerFrame* triggers,
    ProjectControlRuntimeState& state,
    float* sourceValues
);

[[nodiscard]] float applySlew(
    float previous,
    float target,
    uint16_t slewMs,
    uint32_t elapsedMs
);

}  // namespace core::state::modulation::project_control_runtime_detail

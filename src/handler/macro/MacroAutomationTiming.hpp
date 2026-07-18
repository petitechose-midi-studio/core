#pragma once

#include <cstdint>

#include "state/modulation/ProjectControlRuntime.hpp"
#include "state/modulation/ProjectModulationRuntimePlan.hpp"

namespace core::handler::macro {

/** One bounded cadence for live capture and playback projection. */
inline constexpr uint32_t MACRO_AUTOMATION_UPDATE_PERIOD_MS = 16U;
inline constexpr uint32_t MACRO_AUTOMATION_MIN_UPDATE_PERIOD_MS = 1U;
inline constexpr uint8_t MACRO_AUTOMATION_SAMPLES_PER_FAST_CYCLE = 8U;

/** Converts one musical duration to wall time from bounded clock telemetry. */
[[nodiscard]] uint32_t musicalPeriodMilliseconds(
    uint32_t periodTicks,
    const core::state::modulation::ProjectControlTimeTelemetry& telemetry
);

/**
 * Selects the cheapest cadence that still resolves the fastest live motion.
 * Workload back-pressure bounds total candidate evaluations on dense Projects.
 */
[[nodiscard]] uint32_t projectControlUpdatePeriodMilliseconds(
    const core::state::modulation::ProjectModulationRuntimePlan& plan,
    const core::state::modulation::ProjectCurveArena& curves,
    const core::state::modulation::ProjectControlTimeTelemetry& telemetry,
    uint16_t activeAuthorCount
);

}  // namespace core::handler::macro

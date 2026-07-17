#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/modulation/ProjectControlRuntime.hpp"

namespace core::validation::project {

enum class ProjectModulationBenchmarkCase : uint8_t {
    LFO = 0,
    RECORDED_SHAPE,
};

inline constexpr uint32_t PROJECT_MODULATION_BENCHMARK_WARMUP_FRAMES = 64U;
inline constexpr uint32_t PROJECT_MODULATION_BENCHMARK_MEASURED_FRAMES = 2048U;
inline constexpr uint32_t PROJECT_MODULATION_BENCHMARK_AVERAGE_LIMIT_US = 500U;
inline constexpr uint32_t PROJECT_MODULATION_BENCHMARK_MAXIMUM_LIMIT_US = 1000U;

/**
 * Benchmark-only storage. The hardware entry point places this complete object
 * in EXTMEM; it is never part of an ordinary product image.
 */
struct ProjectModulationBenchmarkWorkspace {
    core::state::modulation::ProjectControlDomainState domain{};
    core::state::modulation::ProjectModulationRuntimePlan plan{};
    core::state::modulation::ProjectControlRuntimeState runtime{};
    std::array<
        float,
        core::state::modulation::PROJECT_MODULATOR_CAPACITY
    > sourceValues{};
    std::array<
        core::state::modulation::ProjectLogicalMacroBaseInput,
        core::state::modulation::PROJECT_MODULATION_LIVE_DESTINATION_CAPACITY
    > bases{};
};

struct ProjectModulationBenchmarkResult {
    ProjectModulationBenchmarkCase benchmarkCase =
        ProjectModulationBenchmarkCase::LFO;
    uint32_t iterations = 0;
    uint32_t averageUs = 0;
    uint32_t maximumUs = 0;
    uint32_t checksum = 0;
    uint16_t sourceCount = 0;
    uint16_t bindingCount = 0;
    uint16_t destinationCount = 0;
    bool prepared = false;
    bool evaluated = false;

    [[nodiscard]] bool withinBudget() const {
        return prepared && evaluated &&
            averageUs < PROJECT_MODULATION_BENCHMARK_AVERAGE_LIMIT_US &&
            maximumUs < PROJECT_MODULATION_BENCHMARK_MAXIMUM_LIMIT_US;
    }
};

[[nodiscard]] const char* projectModulationBenchmarkCaseLabel(
    ProjectModulationBenchmarkCase benchmarkCase
);

/** Builds and compiles one exact 128-source/512-binding/128-destination graph. */
[[nodiscard]] bool prepareProjectModulationBenchmark(
    ProjectModulationBenchmarkWorkspace& workspace,
    ProjectModulationBenchmarkCase benchmarkCase
);

/** Measures only the allocation-free product evaluator after bounded warmup. */
[[nodiscard]] ProjectModulationBenchmarkResult runProjectModulationBenchmark(
    ProjectModulationBenchmarkWorkspace& workspace,
    ProjectModulationBenchmarkCase benchmarkCase,
    uint32_t warmupFrames = PROJECT_MODULATION_BENCHMARK_WARMUP_FRAMES,
    uint32_t measuredFrames = PROJECT_MODULATION_BENCHMARK_MEASURED_FRAMES
);

static_assert(sizeof(ProjectModulationBenchmarkWorkspace) == 185668U);
static_assert(std::is_trivially_copyable_v<ProjectModulationBenchmarkWorkspace>);

}  // namespace core::validation::project

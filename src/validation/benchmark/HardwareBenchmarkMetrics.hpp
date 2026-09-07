#pragma once

#if defined(MS_HARDWARE_BENCHMARK)
#include <array>
#include <cstddef>
#include <cstdint>

namespace core::validation::benchmark {

struct Metric {
    const char* name = nullptr;
    uint32_t count = 0;
    uint32_t maxUs = 0;
    // First maximum's callback time relative to run start, not scope origin.
    uint32_t maxAtUs = 0;
    uint32_t unitAMax = 0;
    uint32_t unitBMax = 0;
    uint64_t totalUs = 0;
    // 0: zero; n=1..32: [2^(n-1), 2^n-1] microseconds.
    std::array<uint32_t, 33> bins{};
};

// Foreground lifecycle; begin clears retained results while the sink is off.
// Replaces the process-wide performance sink; do not run PerformanceReporter.
void beginMetrics(uint32_t startedAtUs);
void endMetrics();
// Approximate start-boundary exclusions; the existing sample ABI has no origin.
uint32_t boundarySkipped();
size_t metricCount();
// IRQ-safe scalar snapshot used to prove output activity during injected stalls.
uint32_t metricSamples(const char* name);
// Read only after endMetrics. Invalid indices return an unnamed empty metric.
const Metric& metric(size_t index);

}  // namespace core::validation::benchmark
#endif

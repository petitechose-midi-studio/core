#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include <oc/Config.hpp>

#if OC_ENABLE_STATS
#include <oc/diagnostics/Performance.hpp>
#endif

namespace core::diagnostics {

#if OC_ENABLE_STATS

/**
 * Allocation-free aggregation for opt-in performance samples.
 *
 * Producers only enqueue compact samples through the framework diagnostics
 * sink. Aggregation and logging happen later from the application loop so
 * measured code never formats log messages itself.
 */
class PerformanceReporter {
public:
    void begin();
    void end();
    void update(uint32_t nowMs);

private:
    static constexpr size_t SAMPLE_CAPACITY = 256;
    static constexpr size_t MAX_DRAIN_PER_UPDATE = 64;
    static constexpr size_t METRIC_CAPACITY = 128;
    static constexpr size_t MAX_REPORTED_METRICS = 12;
    // Zero plus two half-octave buckets for every uint32_t duration octave.
    // This keeps percentile storage bounded while covering sub-us/no-op
    // samples through multi-second stalls.
    static constexpr size_t DURATION_BUCKET_CAPACITY = 65;
    static constexpr uint32_t REPORT_INTERVAL_MS = 2000;
    static constexpr uint32_t MEMORY_REPORT_INTERVAL_MS = 30000;

    struct MetricWindow {
        const char* label = nullptr;
        uint32_t samples = 0;
        uint64_t totalUs = 0;
        uint32_t maxUs = 0;
        uint64_t totalUnitA = 0;
        uint64_t totalUnitB = 0;
        uint32_t minUnitA = 0;
        uint32_t maxUnitA = 0;
        uint32_t minUnitB = 0;
        uint32_t maxUnitB = 0;
        std::array<uint32_t, DURATION_BUCKET_CAPACITY> durationBuckets{};
    };

    static void receive_(void* context, const oc::diagnostics::PerformanceSample& sample);
    void enqueue_(const oc::diagnostics::PerformanceSample& sample);
    bool dequeue_(oc::diagnostics::PerformanceSample& sample);
    uint32_t takeDroppedSamples_();
    void drain_();
    MetricWindow* findOrCreateMetric_(const char* label);
    static size_t durationBucket_(uint32_t elapsedUs);
    static uint32_t durationBucketUpperBound_(size_t bucket);
    static uint32_t percentileUs_(
        const MetricWindow& metric,
        uint32_t percentile
    );
    static bool alwaysReport_(const char* label);
    static void reportMetric_(const MetricWindow& metric);
    void report_(uint32_t nowMs);
    void resetAll_();
    void resetMetrics_();

    std::array<oc::diagnostics::PerformanceSample, SAMPLE_CAPACITY> samples_{};
    std::array<MetricWindow, METRIC_CAPACITY> metrics_{};
    size_t sampleHead_ = 0;
    size_t sampleTail_ = 0;
    size_t sampleCount_ = 0;
    size_t metricCount_ = 0;
    uint32_t droppedSamples_ = 0;
    uint32_t droppedMetrics_ = 0;
    uint32_t windowStartedAtMs_ = 0;
    uint32_t lastMemoryReportAtMs_ = 0;
};

/** Returns the RAM2-backed diagnostics reporter singleton. */
PerformanceReporter& performanceReporter();

#endif

}  // namespace core::diagnostics

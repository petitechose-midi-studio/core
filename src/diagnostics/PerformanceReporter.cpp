#include "diagnostics/PerformanceReporter.hpp"

#if OC_ENABLE_STATS

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/realtime/InterruptGuard.hpp>

#include "diagnostics/MemoryFootprintReporter.hpp"

namespace core::diagnostics {

namespace {

alignas(PerformanceReporter)
DMAMEM uint8_t reporterStorage[sizeof(PerformanceReporter)];

}  // namespace

PerformanceReporter& performanceReporter() {
    static PerformanceReporter* instance =
        new (reporterStorage) PerformanceReporter;
    return *instance;
}

void PerformanceReporter::begin() {
    if (!metrics_) metrics_ = core::app::makeExtmemUniqueCold<Metrics>();
    if (!reportingMetrics_) reportingMetrics_ = core::app::makeExtmemUniqueCold<Metrics>();
    if (!metrics_ || !reportingMetrics_) {
        OC_LOG_WARN("[Perf] disabled: PSRAM histogram allocation failed");
        return;
    }
    resetAll_();
    oc::diagnostics::setPerformanceSink(this, receive_);
}

void PerformanceReporter::end() {
    oc::diagnostics::clearPerformanceSink();
    resetAll_();
}

void PerformanceReporter::update(uint32_t nowMs) {
    if (!metrics_ || !reportingMetrics_) return;
    drain_();
    if (windowStartedAtMs_ == 0) {
        windowStartedAtMs_ = nowMs;
    } else if (
        static_cast<uint32_t>(nowMs - windowStartedAtMs_) >=
        REPORT_INTERVAL_MS && reportPosition_ == reportCount_ &&
        reportDroppedSamples_ == 0U && reportDroppedMetrics_ == 0U
    ) {
        freezeWindow_(nowMs);
    }
    if (lastMemoryReportAtMs_ == 0U) {
        lastMemoryReportAtMs_ = nowMs;
    } else if (
        static_cast<uint32_t>(nowMs - lastMemoryReportAtMs_) >=
        MEMORY_REPORT_INTERVAL_MS
    ) {
        memorySection_ = MemoryReportSection::LVGL;
        lastMemoryReportAtMs_ = nowMs;
    }
    // Return to the application (and USB service) after at most one log line.
    // Memory sections are also spread out, never added to a performance line.
    if (memorySection_ != MemoryReportSection::COUNT) {
        OC_PERF_SCOPE(perfMemory, "diagnostics.memory-line");
        logMemoryFootprintSection("runtime-window", memorySection_);
        memorySection_ = static_cast<MemoryReportSection>(
            static_cast<uint8_t>(memorySection_) + 1U);
    } else {
        reportNext_();
    }
}

void PerformanceReporter::receive_(
    void* context,
    const oc::diagnostics::PerformanceSample& sample
) {
    static_cast<PerformanceReporter*>(context)->enqueue_(sample);
}

void PerformanceReporter::enqueue_(const oc::diagnostics::PerformanceSample& sample) {
    oc::realtime::InterruptGuard lock;
    if (sampleCount_ >= samples_.size()) {
        ++droppedSamples_;
        return;
    }

    samples_[sampleTail_] = sample;
    sampleTail_ = (sampleTail_ + 1U) % samples_.size();
    ++sampleCount_;
}

bool PerformanceReporter::dequeue_(oc::diagnostics::PerformanceSample& sample) {
    oc::realtime::InterruptGuard lock;
    if (sampleCount_ == 0) return false;

    sample = samples_[sampleHead_];
    sampleHead_ = (sampleHead_ + 1U) % samples_.size();
    --sampleCount_;
    return true;
}

uint32_t PerformanceReporter::takeDroppedSamples_() {
    oc::realtime::InterruptGuard lock;
    const uint32_t dropped = droppedSamples_;
    droppedSamples_ = 0;
    return dropped;
}

void PerformanceReporter::drain_() {
    oc::diagnostics::PerformanceSample sample{};
    size_t drained = 0;
    while (drained < MAX_DRAIN_PER_UPDATE && dequeue_(sample)) {
        ++drained;
        auto* metric = findOrCreateMetric_(sample.label);
        if (metric == nullptr) continue;

        const bool first = metric->samples == 0U;
        ++metric->samples;
        metric->totalUs += sample.elapsedUs;
        metric->maxUs = std::max(metric->maxUs, sample.elapsedUs);
        metric->totalUnitA += sample.unitA;
        metric->totalUnitB += sample.unitB;
        if (first) {
            metric->minUnitA = sample.unitA;
            metric->maxUnitA = sample.unitA;
            metric->minUnitB = sample.unitB;
            metric->maxUnitB = sample.unitB;
        } else {
            metric->minUnitA = std::min(metric->minUnitA, sample.unitA);
            metric->maxUnitA = std::max(metric->maxUnitA, sample.unitA);
            metric->minUnitB = std::min(metric->minUnitB, sample.unitB);
            metric->maxUnitB = std::max(metric->maxUnitB, sample.unitB);
        }
        ++metric->durationBuckets[durationBucket_(sample.elapsedUs)];
    }
}

PerformanceReporter::MetricWindow* PerformanceReporter::findOrCreateMetric_(
    const char* label
) {
    const char* effectiveLabel = label != nullptr ? label : "<unnamed>";
    for (size_t index = 0; index < metricCount_; ++index) {
        auto& metric = (*metrics_)[index];
        if (metric.label == effectiveLabel || std::strcmp(metric.label, effectiveLabel) == 0) {
            return &metric;
        }
    }

    if (metricCount_ >= metrics_->size()) {
        ++droppedMetrics_;
        return nullptr;
    }

    auto& metric = (*metrics_)[metricCount_++];
    metric = {};
    metric.label = effectiveLabel;
    return &metric;
}

size_t PerformanceReporter::durationBucket_(uint32_t elapsedUs) {
    if (elapsedUs == 0U) return 0U;
    const uint32_t exponent = 31U - static_cast<uint32_t>(
        __builtin_clz(elapsedUs)
    );
    const uint32_t base = UINT32_C(1) << exponent;
    const uint32_t upperHalf =
        exponent > 0U && elapsedUs - base >= base / 2U ? 1U : 0U;
    return std::min<size_t>(
        1U + static_cast<size_t>(exponent) * 2U + upperHalf,
        DURATION_BUCKET_CAPACITY - 1U
    );
}

uint32_t PerformanceReporter::durationBucketUpperBound_(size_t bucket) {
    if (bucket == 0U) return 0U;
    const size_t encoded = bucket - 1U;
    const uint32_t exponent = static_cast<uint32_t>(encoded / 2U);
    const bool upperHalf = (encoded & 1U) != 0U;
    const uint64_t base = UINT64_C(1) << exponent;
    const uint64_t exclusive = upperHalf
        ? base * 2U
        : base + (exponent > 0U ? base / 2U : 1U);
    return static_cast<uint32_t>(std::min<uint64_t>(
        exclusive - 1U,
        UINT32_MAX
    ));
}

uint32_t PerformanceReporter::percentileUs_(
    const MetricWindow& metric,
    uint32_t percentile
) {
    if (metric.samples == 0U) return 0U;
    const uint64_t target = std::max<uint64_t>(
        1U,
        (static_cast<uint64_t>(metric.samples) * percentile + 99U) / 100U
    );
    uint64_t cumulative = 0U;
    for (size_t bucket = 0U;
         bucket < metric.durationBuckets.size();
         ++bucket) {
        cumulative += metric.durationBuckets[bucket];
        if (cumulative >= target) {
            return durationBucketUpperBound_(bucket);
        }
    }
    return metric.maxUs;
}

bool PerformanceReporter::alwaysReport_(const char* label) {
    if (label == nullptr) return false;
    return std::strncmp(label, "memory.", 7U) == 0 ||
        std::strncmp(label, "diagnostics.", 12U) == 0 ||
        std::strncmp(label, "display.ili9341.", 16U) == 0 ||
        std::strncmp(label, "midi.cc.global", 14U) == 0 ||
        std::strncmp(label, "midi.queue.", 11U) == 0 ||
        std::strncmp(label, "macro.take.begin.", 17U) == 0 ||
        std::strncmp(label, "macro.take.commit.", 18U) == 0 ||
        std::strncmp(label, "persistence.project-codec.", 26U) == 0 ||
        std::strncmp(label, "persistence.project-control.", 28U) == 0 ||
        std::strncmp(label, "sequencer.timer", 15U) == 0 ||
        std::strncmp(label, "sequencer.playback", 18U) == 0 ||
        std::strcmp(label, "sequencer.clip-apply") == 0 ||
        std::strncmp(label, "midi.usb-", 9U) == 0 ||
        std::strstr(label, "reject") != nullptr ||
        std::strstr(label, "overflow") != nullptr;
}

void PerformanceReporter::reportMetric_(const MetricWindow& metric, uint32_t windowEndMs) {
    OC_LOG_INFO(
        "[Perf] {} samples={} avg={}us p50<={}us p95<={}us p99<={}us max={}us unitA(avg/min/max)={}/{}/{} unitB(avg/min/max)={}/{}/{} windowEnd={}ms",
        metric.label,
        metric.samples,
        static_cast<uint32_t>(metric.totalUs / metric.samples),
        percentileUs_(metric, 50U),
        percentileUs_(metric, 95U),
        percentileUs_(metric, 99U),
        metric.maxUs,
        static_cast<uint32_t>(metric.totalUnitA / metric.samples),
        metric.minUnitA,
        metric.maxUnitA,
        static_cast<uint32_t>(metric.totalUnitB / metric.samples),
        metric.minUnitB,
        metric.maxUnitB,
        windowEndMs
    );
}

void PerformanceReporter::freezeWindow_(uint32_t nowMs) {
    reportDroppedSamples_ = takeDroppedSamples_();
    reportDroppedMetrics_ = droppedMetrics_;
    reportWindowEndMs_ = nowMs;
    size_t activeCount = 0;
    for (size_t index = 0; index < metricCount_; ++index) {
        if ((*metrics_)[index].samples > 0) {
            reportIndices_[activeCount++] = static_cast<uint8_t>(index);
        }
    }

    std::sort(reportIndices_.begin(), reportIndices_.begin() + activeCount, [this](size_t lhs, size_t rhs) {
        return (*metrics_)[lhs].maxUs > (*metrics_)[rhs].maxUs;
    });

    reportCount_ = std::min(activeCount, MAX_REPORTED_METRICS);
    for (size_t order = reportCount_; order < activeCount; ++order) {
        const auto index = reportIndices_[order];
        if (alwaysReport_((*metrics_)[index].label)) {
            reportIndices_[reportCount_++] = index;
        }
    }
    reportPosition_ = 0;
    metrics_.swap(reportingMetrics_);
    resetMetrics_();
    windowStartedAtMs_ = nowMs;
}

void PerformanceReporter::reportNext_() {
    if (reportPosition_ < reportCount_) {
        OC_PERF_SCOPE(perfReport, "diagnostics.report-line");
        reportMetric_((*reportingMetrics_)[reportIndices_[reportPosition_++]], reportWindowEndMs_);
        return;
    }
    if (reportDroppedSamples_ > 0 || reportDroppedMetrics_ > 0) {
        OC_LOG_WARN(
            "[Perf] diagnostics overflow samples={} metrics={} windowEnd={}ms",
            reportDroppedSamples_,
            reportDroppedMetrics_,
            reportWindowEndMs_
        );
        reportDroppedSamples_ = 0;
        reportDroppedMetrics_ = 0;
    }
}

void PerformanceReporter::resetAll_() {
    // begin() calls this before installing the sink and end() calls it after
    // clearing the sink, so no producer can observe these indices. The sample
    // payload does not need clearing and must not be zeroed under an IRQ guard.
    sampleHead_ = 0;
    sampleTail_ = 0;
    sampleCount_ = 0;
    droppedSamples_ = 0;
    resetMetrics_();
    windowStartedAtMs_ = 0;
    lastMemoryReportAtMs_ = 0;
    reportCount_ = 0;
    reportPosition_ = 0;
    reportDroppedSamples_ = 0;
    reportDroppedMetrics_ = 0;
    reportWindowEndMs_ = 0;
    memorySection_ = MemoryReportSection::COUNT;
}

void PerformanceReporter::resetMetrics_() {
    // findOrCreateMetric_ initializes each slot before reuse. Inactive slots
    // need no second full histogram clear at the end of every window.
    metricCount_ = 0;
    droppedMetrics_ = 0;
}

}  // namespace core::diagnostics

#endif

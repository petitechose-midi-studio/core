#include "diagnostics/PerformanceReporter.hpp"

#if OC_ENABLE_STATS

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <new>

#include <config/PlatformCompat.hpp>
#include <oc/log/Log.hpp>
#include <oc/realtime/InterruptGuard.hpp>

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
    resetAll_();
    oc::diagnostics::setPerformanceSink(this, receive_);
}

void PerformanceReporter::end() {
    oc::diagnostics::clearPerformanceSink();
    resetAll_();
}

void PerformanceReporter::update(uint32_t nowMs) {
    drain_();
    if (windowStartedAtMs_ == 0) {
        windowStartedAtMs_ = nowMs;
        return;
    }
    if (static_cast<uint32_t>(nowMs - windowStartedAtMs_) >= REPORT_INTERVAL_MS) {
        report_(nowMs);
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

        ++metric->samples;
        metric->totalUs += sample.elapsedUs;
        metric->maxUs = std::max(metric->maxUs, sample.elapsedUs);
        metric->totalUnitA += sample.unitA;
        metric->totalUnitB += sample.unitB;
    }
}

PerformanceReporter::MetricWindow* PerformanceReporter::findOrCreateMetric_(
    const char* label
) {
    const char* effectiveLabel = label != nullptr ? label : "<unnamed>";
    for (size_t index = 0; index < metricCount_; ++index) {
        auto& metric = metrics_[index];
        if (metric.label == effectiveLabel || std::strcmp(metric.label, effectiveLabel) == 0) {
            return &metric;
        }
    }

    if (metricCount_ >= metrics_.size()) {
        ++droppedMetrics_;
        return nullptr;
    }

    auto& metric = metrics_[metricCount_++];
    metric = {};
    metric.label = effectiveLabel;
    return &metric;
}

void PerformanceReporter::report_(uint32_t nowMs) {
    const uint32_t droppedSamples = takeDroppedSamples_();
    std::array<size_t, METRIC_CAPACITY> indices{};
    size_t activeCount = 0;
    for (size_t index = 0; index < metricCount_; ++index) {
        if (metrics_[index].samples > 0) indices[activeCount++] = index;
    }

    std::sort(indices.begin(), indices.begin() + activeCount, [this](size_t lhs, size_t rhs) {
        return metrics_[lhs].maxUs > metrics_[rhs].maxUs;
    });

    const size_t reportCount = std::min(activeCount, MAX_REPORTED_METRICS);
    for (size_t order = 0; order < reportCount; ++order) {
        const auto& metric = metrics_[indices[order]];
        OC_LOG_INFO(
            "[Perf] {} samples={} avg={}us max={}us unitAAvg={} unitBAvg={}",
            metric.label,
            metric.samples,
            static_cast<uint32_t>(metric.totalUs / metric.samples),
            metric.maxUs,
            static_cast<uint32_t>(metric.totalUnitA / metric.samples),
            static_cast<uint32_t>(metric.totalUnitB / metric.samples)
        );
    }

    if (droppedSamples > 0 || droppedMetrics_ > 0) {
        OC_LOG_WARN(
            "[Perf] diagnostics overflow samples={} metrics={}",
            droppedSamples,
            droppedMetrics_
        );
    }

    resetMetrics_();
    windowStartedAtMs_ = nowMs;
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
}

void PerformanceReporter::resetMetrics_() {
    for (size_t index = 0; index < metricCount_; ++index) {
        metrics_[index] = {};
    }
    metricCount_ = 0;
    droppedMetrics_ = 0;
}

}  // namespace core::diagnostics

#endif

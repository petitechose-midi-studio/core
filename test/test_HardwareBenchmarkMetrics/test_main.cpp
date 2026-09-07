#include <cassert>
#include <cstdint>
#include <cstring>

#include <oc/diagnostics/Performance.hpp>
#include <oc/time/Time.hpp>
#include "validation/benchmark/HardwareBenchmarkMetrics.hpp"

namespace bench = core::validation::benchmark;
namespace {
uint32_t nowUs = 0;
bool stopDuringReceive = false;
uint32_t clock() {
    if (stopDuringReceive) {
        stopDuringReceive = false;
        bench::endMetrics();
    }
    return nowUs;
}
const bench::Metric& find(const char* name) {
    for (size_t index = 0; index < bench::metricCount(); ++index)
        if (std::strcmp(bench::metric(index).name, name) == 0) return bench::metric(index);
    assert(false); return bench::metric(bench::metricCount());
}
}

int main() {
    oc::time::setMicrosProvider(clock);
    assert(bench::metricCount() == 0U && bench::metric(0).name == nullptr);
    assert(bench::metricSamples("main.loop") == 0U);
    bench::beginMetrics(100U);
    assert(bench::metricCount() > 0U);
    nowUs = 105U;
    oc::diagnostics::recordPerformance({"main.loop", 6U, 0U, 0U}); // Began before run.
    assert(find("main.loop").count == 0U);
    assert(bench::boundarySkipped() == 1U);
    nowUs = 200U;
    for (const char* name : {"midi.usb-admission-refused", "midi.usb-session-reset",
                            "midi.usb-output-irq", "midi.usb-wake-age", "benchmark.foreground-block",
                            "midi.cc.stop-cancel", "midi.cc.stop-sync",
                            "sequencer.cc.frame", "sequencer.cc.validate", "sequencer.cc.project",
                            "sequencer.cc.route", "sequencer.cc.inputs", "sequencer.cc.compose",
                            "sequencer.playback-sync", "sequencer.playback-reconcile", "sequencer.playback-engines",
                            "midi.cc.track-clear", "display.ili9341.submit-diff",
                            "display.ili9341.submit-other", "display.ili9341.submit-busy"}) {
        oc::diagnostics::recordPerformance({name, 3U, 1U, 2U});
        assert(find(name).count == 1U && find(name).unitBMax == 2U);
    }
    for (uint32_t duration : {0U, 1U, 2U, 3U, 4U, 7U, 8U})
        oc::diagnostics::recordPerformance({"main.loop", duration, duration + 1U, 100U - duration});
    char equalName[] = "main.loop";
    oc::diagnostics::recordPerformance({equalName, 16U, 1000U, 2000U});
    oc::diagnostics::recordPerformance({"not.whitelisted", 9U, 9U, 9U});
    oc::diagnostics::recordPerformance({nullptr, 9U, 9U, 9U});
    const auto& item = find("main.loop");
    assert(item.count == 8U && item.totalUs == 41U && item.maxUs == 16U);
    assert(bench::metricSamples("main.loop") == 8U);
    assert(bench::metricSamples(nullptr) == 0U && bench::metricSamples("unknown") == 0U);
    assert(item.maxAtUs == 100U);
    assert(item.unitAMax == 1000U && item.unitBMax == 2000U);
    assert(item.bins[0] == 1U && item.bins[1] == 1U && item.bins[2] == 2U);
    assert(item.bins[3] == 2U && item.bins[4] == 1U && item.bins[5] == 1U);
    assert(bench::metric(999U).name == nullptr);

    nowUs = 220U;
    oc::diagnostics::recordPerformance({"sequencer.timer", 0U, 0U, 0U});
    assert(find("sequencer.timer").maxAtUs == 120U); // First zero-duration sample is located.
    nowUs = 225U;
    oc::diagnostics::recordPerformance({"sequencer.timer", 0U, 0U, 0U});
    assert(find("sequencer.timer").maxAtUs == 120U); // Equal maxima retain first occurrence.
    nowUs = 230U;
    oc::diagnostics::recordPerformance({"sequencer.timer", 10U, 0U, 0U});
    assert(find("sequencer.timer").maxAtUs == 130U);

    nowUs = 300U;
    stopDuringReceive = true; // A callback already loaded when capture closes.
    oc::diagnostics::recordPerformance({"main.loop", 99U, 0U, 0U});
    assert(find("main.loop").count == 8U);
    assert(find("main.loop").maxAtUs == 100U);
    oc::diagnostics::recordPerformance({"main.loop", 99U, 0U, 0U});
    assert(find("main.loop").count == 8U);

    nowUs = 400U;
    {
        oc::diagnostics::PerformanceScope beforeRun("main.loop");
        nowUs = 410U;
        bench::beginMetrics(nowUs);
        nowUs = 415U;
    }
    assert(find("main.loop").count == 0U && bench::boundarySkipped() == 1U);
    {
        oc::diagnostics::PerformanceScope afterStop("main.loop");
        nowUs = 420U;
        bench::endMetrics();
        nowUs = 425U;
    }
    assert(find("main.loop").count == 0U);

    nowUs = UINT32_MAX - 10U;
    bench::beginMetrics(nowUs);
    nowUs += 20U;
    oc::diagnostics::recordPerformance({"sequencer.timer", 12U, 1U, 2U});
    oc::diagnostics::recordPerformance({"sequencer.timer", 21U, 9U, 9U});
    assert(find("sequencer.timer").count == 1U && find("sequencer.timer").maxUs == 12U);
    assert(find("sequencer.timer").maxAtUs == 20U);
    assert(find("main.loop").count == 0U);
    assert(bench::boundarySkipped() == 1U);
    bench::endMetrics();

    // Exercise every whitelist entry, including equal text at another address.
    nowUs = 1000U;
    bench::beginMetrics(0U);
    const size_t count = bench::metricCount();
    for (size_t i = 0; i < count; ++i) {
        char copiedName[128]{};
        const char* name = bench::metric(i).name;
        assert(std::strlen(name) < sizeof(copiedName));
        std::strcpy(copiedName, name);
        oc::diagnostics::recordPerformance({copiedName, 7U, 3U, 5U});
        assert(bench::metricSamples(name) == 1U);
    }
    for (const char* name : {"", "app", "main.loop.extra", "zzzz"}) {
        oc::diagnostics::recordPerformance({name, 9U, 0U, 0U});
        assert(bench::metricSamples(name) == 0U);
    }
    bench::endMetrics();
    for (size_t i = 0; i < count; ++i)
        assert(bench::metric(i).count == 1U && bench::metric(i).totalUs == 7U);
}

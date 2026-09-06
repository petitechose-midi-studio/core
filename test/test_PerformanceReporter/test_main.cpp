#include <algorithm>
#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <string>

#include <oc/log/Log.hpp>
#include "diagnostics/PerformanceReporter.hpp"

namespace {
std::string output;
uint32_t memoryReports = 0;
std::array<uint32_t, 5> memorySections{};

void pump(core::diagnostics::PerformanceReporter& reporter, uint32_t now, bool playing = false) {
    for (uint32_t tick = now; tick < now + 120U; ++tick) {
        const auto begin = output.size();
        const auto beforeMemory = memoryReports;
        reporter.update(tick, playing);
        const auto lines = std::count(output.begin() + begin, output.end(), '\n');
        assert(lines + memoryReports - beforeMemory <= 1U);
    }
}

void installOutput() {
    oc::log::setOutput({
        [](char v) { output += v; },
        [](const char* v) { output += v; },
        [](int32_t v) { output += std::to_string(v); },
        [](uint32_t v) { output += std::to_string(v); },
        [](float v) { output += std::to_string(v); },
        [](bool v) { output += v ? "true" : "false"; },
        []() -> uint32_t { return 0; },
    });
}
} // namespace

// Hardware memory inspection is not part of this native collector test.
namespace core::diagnostics {
void logMemoryFootprintSection(const char*, MemoryReportSection section) {
    ++memorySections[static_cast<size_t>(section)];
    ++memoryReports;
}
}

int main() {
    installOutput();
    {
        core::diagnostics::PerformanceReporter failedSecondBuffer;
        core::app::testing::failExtmemAllocationOn(2);
        failedSecondBuffer.begin();
        failedSecondBuffer.update(40000);
        assert(memoryReports == 0);
        assert(output.find("disabled: PSRAM histogram allocation failed") != std::string::npos);
        failedSecondBuffer.end();
        output.clear();
    }
    core::diagnostics::PerformanceReporter reporter;
    core::app::testing::failExtmemAllocationOn(1);
    reporter.begin();
    reporter.update(1);
    reporter.update(40000);
    assert(output.find("disabled: PSRAM histogram allocation failed") != std::string::npos);
    assert(memoryReports == 0);
    reporter.end();

    output.clear();
    reporter.begin();
    reporter.update(1);
    std::array<std::string, 13> ordinary;
    for (size_t i = 0; i < ordinary.size(); ++i) {
        ordinary[i] = "ordinary." + std::to_string(i);
        oc::diagnostics::recordPerformance({ordinary[i].c_str(), 10000, 0, 0});
    }
    // Critical counters must survive the top-12 display filter even when their
    // duration is zero (clip lateness is a musical-tick unit, not a duration).
    constexpr std::array<const char*, 6> critical{
        "sequencer.timer-entry-gap", "sequencer.timer-ui-capture",
        "sequencer.playback-engines", "sequencer.clip-apply",
        "midi.usb-queue-age", "midi.usb-send-max",
    };
    for (const auto* label : critical) {
        oc::diagnostics::recordPerformance({label, 0, 7, 2});
    }
    reporter.update(2001);
    assert(std::count(output.begin(), output.end(), '\n') == 1);
    // Collection continues during emission, without contaminating the frozen window.
    oc::diagnostics::recordPerformance({"next.window", 12, 0, 0});
    pump(reporter, 2002);
    assert(output.find("next.window") == std::string::npos);
    assert(output.find("windowEnd=2001ms") != std::string::npos);
    for (const auto* label : critical) {
        assert(output.find(std::string(label) + " samples=1") != std::string::npos);
    }
    assert(output.find("unitA(avg/min/max)=7/7/7") != std::string::npos);

    output.clear();
    pump(reporter, 4001);
    assert(output.find("next.window samples=1 avg=12us") != std::string::npos);
    assert(output.find("ordinary.") == std::string::npos);
    assert(output.find("sequencer.clip-apply") == std::string::npos);

    output.clear();
    reporter.end();
    reporter.begin();
    for (int i = 0; i < 300; ++i) {
        oc::diagnostics::recordPerformance({"overflow.test", 1, 0, 0});
    }
    oc::diagnostics::recordPerformance({"main.loop", 120000, 3, 4});
    oc::diagnostics::recordPerformance({"smaller.duration", 22000, 0, 0});
    oc::diagnostics::recordPerformance({"midi.usb-service-gap", 130000, 7, 8});
    oc::diagnostics::recordPerformance({"midi.usb-queue-age", 119000, 0, 0});
    oc::diagnostics::recordPerformance({"diagnostics.report-line", 118000, 0, 0});
    for (uint32_t now = 4200; now < 4204; ++now) reporter.update(now);
    pump(reporter, 6200);
    assert(output.find("overflow.test samples=256") != std::string::npos);
    assert(output.find("diagnostics overflow samples=49 metrics=0") != std::string::npos);
    assert(output.find("dropped-peak label=main.loop elapsed=120000us unitA=3 unitB=4") != std::string::npos);
    assert(output.find("dropped-peak label=midi.usb-service-gap elapsed=130000us") != std::string::npos);
    assert(output.find("dropped-peak label=diagnostics.report-line elapsed=118000us") != std::string::npos);
    assert(output.find("main.loop samples=") == std::string::npos);
    output.clear();
    pump(reporter, 8200);
    assert(output.find("dropped-peak") == std::string::npos);

    // Exhaust the histogram table without exhausting the ingress ring.
    reporter.end();
    reporter.begin();
    std::array<std::string, 96> labels;
    for (size_t i = 0; i < labels.size(); ++i) {
        labels[i] = "metric." + std::to_string(i);
        oc::diagnostics::recordPerformance({labels[i].c_str(), 1, 0, 0});
    }
    pump(reporter, 1);
    oc::diagnostics::recordPerformance({"unregistered.long-span", 42000, 1, 2});
    pump(reporter, 2001);
    assert(output.find("dropped-peak label=unregistered.long-span elapsed=42000us") != std::string::npos);

    reporter.end();
    core::app::testing::failExtmemAllocationOn(1);
    reporter.begin(); // Retained histogram storage: no allocation on restart.
    assert(core::app::testing::extmemAllocationAttempt == 0);
    core::app::testing::resetExtmemAllocationFailure();
    reporter.update(1);
    pump(reporter, 30001);
    assert(memoryReports == 5);
    for (auto count : memorySections) assert(count == 1);
    reporter.end();
    reporter.begin();
    memoryReports = 0;
    memorySections = {};
    reporter.update(1, true);
    pump(reporter, 30001, true);
    assert(memoryReports == 4);
    assert(memorySections[0] == 0); // No LVGL allocation walk during playback.
    pump(reporter, 32001, true);
    assert(memoryReports == 4);
    pump(reporter, 34001, false);
    assert(memoryReports == 5);
    assert(memorySections[0] == 1); // Deferred scan performed once after stop.
    reporter.end();
    output.clear();
    oc::diagnostics::recordPerformance({"after.end", 1, 0, 0});
    assert(output.empty());
    // A slow foreground must finish its pending window instead of overwriting
    // it when another interval expires. Samples arriving meanwhile survive.
    reporter.begin();
    reporter.update(10);
    for (const auto* label : critical) {
        oc::diagnostics::recordPerformance({label, 1, 0, 0});
    }
    reporter.update(2010);
    oc::diagnostics::recordPerformance({"late.foreground", 22, 0, 0});
    pump(reporter, 5000);
    for (const auto* label : critical) {
        const auto at = output.find(std::string(label) + " samples=1");
        assert(at != std::string::npos);
        const auto end = output.find('\n', at);
        assert(output.substr(at, end - at).find("windowEnd=2010ms") != std::string::npos);
    }
    assert(output.find("late.foreground samples=1 avg=22us") != std::string::npos);
    reporter.end();
    reporter.begin();
    output.clear();
    static const char sameLabelA[] = "same.label";
    static const char sameLabelB[] = "same.label";
    reporter.update(1);
    oc::diagnostics::recordPerformance({sameLabelA, 10, 0, 0});
    oc::diagnostics::recordPerformance({sameLabelB, 20, 0, 0});
    pump(reporter, 2001);
    assert(output.find("same.label samples=2 avg=15us") != std::string::npos);
    reporter.end();
    std::cout << "PerformanceReporter: failure, reuse, critical metrics, windows and overflow OK\n";
}

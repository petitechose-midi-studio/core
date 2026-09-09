#include "HardwareBenchmarkMetrics.hpp"

#if defined(MS_HARDWARE_BENCHMARK)
#include <algorithm>
#include <cstring>
#include <string_view>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/realtime/InterruptGuard.hpp>
#include <oc/time/Time.hpp>

#if !OC_ENABLE_STATS
#error "Hardware benchmark metrics require OC_ENABLE_STATS=1"
#endif

namespace core::validation::benchmark {
namespace {
constexpr std::array<const char*, 81> names{
    "app.context",
    "app.input",
    "app.midi-post-drain",
    "app.midi-pre-drain",
    "app.notifications",
    "app.pre-context-hooks",
    "bench.fs.abort-write",
    "bench.fs.append-write",
    "bench.fs.begin-write",
    "bench.fs.finish-write",
    "bench.fs.flush",
    "bench.fs.list",
    "bench.fs.mkdir",
    "bench.fs.read",
    "bench.fs.remove",
    "bench.fs.rename",
    "bench.fs.stat",
    "bench.fs.write",
    "benchmark.foreground-block",
    "display.ili9341.flush-region",
    "display.ili9341.submit-busy",
    "display.ili9341.submit-diff",
    "display.ili9341.submit-other",
    "display.lvgl.flush-callback",
    "display.lvgl.frame-deferred",
    "display.lvgl.refresh",
    "macro.automation-playback",
    "main.app-update",
    "main.core-state",
    "main.filesystem-rpc",
    "main.loop",
    "midi.cc.global-frame",
    "midi.cc.global-reject",
    "midi.cc.global-resolve",
    "midi.cc.global-stage",
    "midi.cc.stop-cancel",
    "midi.cc.stop-sync",
    "midi.cc.track-clear",
    "midi.queue.drain",
    "midi.queue.drop-late-note-on",
    "midi.queue.late-send",
    "midi.queue.note-off-batch",
    "midi.queue.transport-rejected",
    "midi.usb-admission-refused",
    "midi.usb-output-drain",
    "midi.usb-output-irq",
    "midi.usb-queue-age",
    "midi.usb-rejections",
    "midi.usb-send-max",
    "midi.usb-service-gap",
    "midi.usb-session-reset",
    "midi.usb-wake-age",
    "project-control.evaluate",
    "sequencer.cc.compose",
    "sequencer.cc.frame",
    "sequencer.cc.inputs",
    "sequencer.cc.project",
    "sequencer.cc.route",
    "sequencer.cc.validate",
    "sequencer.playback",
    "sequencer.playback-cc",
    "sequencer.playback-engines",
    "sequencer.playback-reconcile",
    "sequencer.playback-sync",
    "sequencer.timer",
    "sequencer.timer-entry-gap",
    "ui.curve-preview.band-draw",
    "ui.curve-preview.draw",
    "ui.curve-preview.geometry",
    "ui.macro-overlay.mutation.detail",
    "ui.macro-overlay.mutation.edit",
    "ui.macro-overlay.projection.detail",
    "ui.macro-overlay.projection.edit",
    "ui.macro.projection.frame",
    "ui.overlay-exclusivity",
    "ui.project.activate",
    "ui.project.modulator-workspace.mutation",
    "ui.project.modulator.cards",
    "ui.project.modulator.curve",
    "ui.project.modulator.header",
    "ui.project.modulator.layout",
};
static_assert([] {
    for (size_t i = 1; i < names.size(); ++i)
        if (std::string_view(names[i - 1]) >= names[i]) return false;
    return true;
}(), "Benchmark metric names must remain sorted and unique");
DMAMEM std::array<Metric, names.size()> metrics;
bool active = false;
bool initialized = false;
uint32_t runStartedAtUs = 0;
uint32_t skippedAtBoundary = 0;

size_t metricIndex(const char* label) {
    if (!label) return names.size();
    const auto found = std::lower_bound(
        names.begin(), names.end(), label,
        [](const char* left, const char* right) { return std::strcmp(left, right) < 0; });
    return found != names.end() && std::strcmp(*found, label) == 0
        ? static_cast<size_t>(found - names.begin()) : names.size();
}

void receive(void*, const oc::diagnostics::PerformanceSample& sample) {
    // Immutable whitelist lookup is outside the IRQ-masked section.
    const size_t index = metricIndex(sample.label);
    if (index == names.size()) return;
    const uint32_t receivedAtUs = oc::time::micros32();
    const size_t bucket = sample.elapsedUs == 0U ? 0U :
        32U - static_cast<uint32_t>(__builtin_clz(sample.elapsedUs));
    oc::realtime::InterruptGuard lock;
    // The callback may have been loaded before endMetrics cleared the sink.
    if (!active) return;
    const uint32_t sinceStart = receivedAtUs - runStartedAtUs;
    // Existing PerformanceSample has no timestamp. Infer its origin from the
    // callback time minus duration: dispatch/preemption delay makes this an
    // approximate boundary, not proof of the exact original scope start.
    // Runs must be shorter than 2^31 us, as with the benchmark deadline clock.
    if (static_cast<int32_t>(sinceStart) < 0 || sample.elapsedUs > sinceStart) {
        if (skippedAtBoundary != UINT32_MAX) ++skippedAtBoundary;
        return;
    }
    auto& output = metrics[index];
    if (output.count == 0 || sample.elapsedUs > output.maxUs) {
        output.maxUs = sample.elapsedUs;
        output.maxAtUs = sinceStart;
    }
    if (output.count != UINT32_MAX) ++output.count;
    if (output.bins[bucket] != UINT32_MAX) ++output.bins[bucket];
    output.totalUs = sample.elapsedUs > UINT64_MAX - output.totalUs
        ? UINT64_MAX : output.totalUs + sample.elapsedUs;
    output.unitAMax = std::max(output.unitAMax, sample.unitA);
    output.unitBMax = std::max(output.unitBMax, sample.unitB);
}
}  // namespace

void beginMetrics(uint32_t startedAtUs) {
    endMetrics();
    // Clear histogram storage while detached, never with interrupts disabled.
    metrics = {};
    for (size_t index = 0; index < names.size(); ++index) metrics[index].name = names[index];
    skippedAtBoundary = 0;
    initialized = true;
    oc::realtime::InterruptGuard lock;
    runStartedAtUs = startedAtUs;
    active = true;
    oc::diagnostics::setPerformanceSink(nullptr, receive);
}

void endMetrics() {
    oc::realtime::InterruptGuard lock;
    active = false;
    oc::diagnostics::clearPerformanceSink();
}

uint32_t boundarySkipped() { return skippedAtBoundary; }

size_t metricCount() { return initialized ? names.size() : 0U; }

uint32_t metricSamples(const char* name) {
    const size_t index = metricIndex(name);
    oc::realtime::InterruptGuard lock;
    return initialized && index < names.size() ? metrics[index].count : 0U;
}

const Metric& metric(size_t index) {
    static const Metric empty{};
    return index < metricCount() ? metrics[index] : empty;
}
}  // namespace core::validation::benchmark
#endif

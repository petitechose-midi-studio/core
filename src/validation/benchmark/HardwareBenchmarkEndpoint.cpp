#include "validation/benchmark/HardwareBenchmarkEndpoint.hpp"

#if defined(MS_HARDWARE_BENCHMARK)
#include <algorithm>
#include <charconv>
#include <cstdio>
#include <cstring>
#include <limits>
#include <lvgl.h>
#include <oc/core/event/Events.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/state/NotificationQueue.hpp>
#include <oc/ui/lvgl/LvglFrameProfiler.hpp>
#include "config/TimeCompat.hpp"
#include "state/CoreState.hpp"
#include "validation/benchmark/HardwareBenchmarkFixture.hpp"
#include "validation/benchmark/BenchInput.hpp"
#include "validation/benchmark/HardwareBenchmarkMetrics.hpp"
#include "validation/ux/SemanticUxRecorder.hpp"

#ifndef MS_HARDWARE_BENCHMARK_BUILD_ID
#include "HardwareBenchmarkBuildIdentity.hpp"
#endif

#ifndef MS_HARDWARE_BENCHMARK_BUILD_ID
#error Hardware benchmark requires a reproducible build identity
#endif

#if defined(ARDUINO_TEENSY41)
extern "C" { extern unsigned long _heap_end; extern char* __brkval; }
#endif

namespace core::validation::benchmark {
namespace profiler = oc::ui::lvgl::benchmark;

namespace {
std::array<char, 21> decimal64(uint64_t value) {
    // Teensy's nano printf does not support %llu. Keep exact 64-bit totals
    // without enabling a larger printf implementation in the firmware.
    std::array<char, 21> text{};
    std::to_chars(text.data(), text.data() + 20, value);
    return text;
}
}

HardwareBenchmarkEndpoint::HardwareBenchmarkEndpoint(
    oc::interface::ITransport& transport, oc::interface::IEventBus& bus,
    core::state::CoreState& state, uint32_t (*midiInputCount)())
    : transport_(transport), bus_(bus), state_(state), midiInputCount_(midiInputCount) {}

HardwareBenchmarkEndpoint::~HardwareBenchmarkEndpoint() {
    for (auto id : inputSubscriptions_) bus_.off(id);
    transport_.setOnReceive(nullptr);
    endMetrics();
    profiler::endRun();
}

void HardwareBenchmarkEndpoint::begin() {
    constexpr oc::type::EventType types[] = {oc::core::event::InputEvent::BUTTON_PRESS,
        oc::core::event::InputEvent::BUTTON_RELEASE, oc::core::event::InputEvent::ENCODER_CHANGED};
    for (size_t i = 0; i < inputSubscriptions_.size(); ++i) {
        inputSubscriptions_[i] = bus_.on(oc::type::EventCategory::USER_INPUT, types[i],
            [this](const oc::type::Event&) {
                // Configuration/setPosition do not emit these HAL events.
                // Keep watching through cleanup, then freeze the result.
                if (!injecting_ && !ready_) {
                    if (physicalInputs_ != UINT32_MAX) ++physicalInputs_;
                    run_.fail("physical_input_contamination");
                }
            });
        if (inputSubscriptions_[i] == 0) run_.fail("input_monitor_capacity");
    }
    transport_.setOnReceive([this](const uint8_t* bytes, size_t size) { receive(bytes, size); });
}

void HardwareBenchmarkEndpoint::observeMidiInput() {
    if (ready_ || !midiInputCount_) return;
    midiInputs_ = std::max(midiInputs_, midiInputCount_());
    if (midiInputs_) run_.fail("midi_input_contamination");
}

void HardwareBenchmarkEndpoint::reply(uint16_t request, RpcStatus status, const char* json) {
    // Output is small and copied by the existing framed transport. No report
    // serialization or filesystem activity is done during a measured run.
    uint8_t packet[sizeof(json_) + 6];
    const size_t size = std::strlen(json);
    if (size >= sizeof(json_)) return;
    packet[0] = RESPONSE_ID; packet[1] = 0; packet[2] = SCHEMA;
    packet[3] = request & 0xff; packet[4] = request >> 8; packet[5] = uint8_t(status);
    std::memcpy(packet + 6, json, size);
    transport_.send(packet, size + 6);
}

void HardwareBenchmarkEndpoint::describe() {
    const char* state = run_.terminal() && !ready_ ? "finishing" : stateName(run_.state());
    std::snprintf(json_, sizeof(json_),
        "{\"benchmark\":true,\"protocol\":1,\"ram_only\":%s,\"requires_reboot\":true,"
        "\"fixture\":\"%s\",\"fixture_version\":\"%s\",\"build_id\":\"%s\","
        "\"max_events\":%u,\"state\":\"%s\",\"run_id\":%lu,\"physical_inputs\":%lu,\"midi_inputs\":%lu}",
        filesystemReceive_ ? "false" : "true", VERSION, VERSION,
        MS_HARDWARE_BENCHMARK_BUILD_ID, unsigned(MAX_EVENTS), state, (unsigned long)run_.id(),
        (unsigned long)physicalInputs_, (unsigned long)midiInputs_);
}

void HardwareBenchmarkEndpoint::receive(const uint8_t* data, size_t size) {
    if (data && size && data[0] == 0xFC && filesystemReceive_) {
        if (run_.state() == RunState::RUNNING && filesystemRequests_ != UINT32_MAX)
            ++filesystemRequests_;
        filesystemReceive_(data, size);
        return;
    }
    if (run_.state() == RunState::RUNNING) ++foreignRequests_;
    observeMidiInput();
    if (size < 6 || data[0] != REQUEST_ID || data[1] != 0 || data[2] != SCHEMA) return;
    const uint16_t request = read16(data + 3);
    const uint8_t op = data[5];
    const uint8_t* body = data + 6;
    const size_t length = size - 6;
    const uint32_t now = core::time_compat::micros();
    // A host must stay silent until duration+settling. Even an innocent poll
    // changes the workload, so retain that contamination in the final verdict.
    switch (op) {
        case 0: case 3:
            if (length != 0) break;
            describe(); reply(request, RpcStatus::OK, json_); return;
        case 1: {
            const auto status = run_.upload(body, length);
            std::snprintf(json_, sizeof(json_),
                "{\"run_id\":%lu,\"count\":%u,\"script_fnv1a\":%lu}",
                (unsigned long)run_.id(), unsigned(run_.count()), (unsigned long)run_.hash());
            reply(request, status, json_); return;
        }
        case 2:
            if (length != 4) break;
            if (physicalInputs_ || midiInputs_) {
                reply(request, RpcStatus::INVALID_STATE, physicalInputs_
                    ? "{\"error\":\"physical_input_contamination\"}"
                    : "{\"error\":\"midi_input_contamination\"}"); return;
            }
            if (run_.state() == RunState::UPLOADED && !allPhysicalReleased()) {
                reply(request, RpcStatus::INVALID_STATE, "{\"error\":\"physical_button_held\"}"); return;
            }
            { const auto status = run_.start(read32(body), now);
              describe(); reply(request, status, json_); return; }
        case 4:
            if (length != 7) break;
            result(read32(body), body[4], read16(body + 5), request); return;
        case 5:
            if (length != 4) break;
            if (read32(body) != run_.id()) { reply(request, RpcStatus::CONFLICT, "{}"); return; }
            cancelRequested_ = true;
            reply(request, RpcStatus::OK, "{\"cancel_requested\":true}"); return;
        case 6:
            if (length != 0) break;
            cancelRequested_ = true;
            rebootAtUs_ = now + 500'000U;
            reply(request, RpcStatus::OK, "{\"rebooting\":true}"); return;
        default: reply(request, RpcStatus::UNSUPPORTED, "{}"); return;
    }
    reply(request, RpcStatus::INVALID_ARGUMENT, "{}");
}

void HardwareBenchmarkEndpoint::capture(Trace& trace) {
    const auto state = core::validation::ux::makeSemanticUxSnapshot(state_);
    trace.view = uint8_t(state.view); trace.overlay = uint8_t(state.overlay);
    trace.playing = state.playing; trace.marker = marker_;
    core::validation::ux::SemanticUxContext context{};
    oc::core::input::InputBindingTraceEvent event{};
    event.buttonId = std::numeric_limits<oc::type::ButtonID>::max();
    event.encoderId = std::numeric_limits<oc::type::EncoderID>::max();
    if (auto* provider = core::validation::ux::currentSemanticUxContextProvider())
        provider->captureSemanticUxContext(event, context);
    // The normal root surfaces classify gestures, not passive snapshots.
    // This fallback is only the benchmark's explicit idle-state projection.
    const char* mode = context.mode ? context.mode :
        (state.view == core::ui::ViewType::MACRO && state.overlay == core::ui::OverlayType::NONE
            ? "macro.performance" : "");
    const size_t length = std::strlen(mode);
    trace.modeHash = fnv1a(reinterpret_cast<const uint8_t*>(mode), length);
    if (length >= sizeof(trace.mode)) run_.fail("semantic_mode_too_long");
    std::snprintf(trace.mode, sizeof(trace.mode), "%s", mode);
    // Mode is a machine identifier; never permit malformed JSON from a label.
    for (char& ch : trace.mode)
        if (ch == '"' || ch == '\\' || (ch != 0 && static_cast<unsigned char>(ch) < 32)) ch = '_';
}

void HardwareBenchmarkEndpoint::execute(const Action& action, uint32_t nowUs) {
    auto& trace = trace_[traceCount_++];
    trace.dueUs = action.dueUs; trace.actualUs = run_.elapsedUs(nowUs);
    trace.latenessUs = run_.latenessUs();
    injecting_ = true;
    switch (action.kind) {
        case ActionKind::BUTTON:
            if (!setSyntheticButton(action.target, action.value != 0)) {
                run_.fail("synthetic_button_unavailable"); break;
            }
            if (action.value) {
                held_ |= uint64_t(1) << action.target;
                bus_.emit(oc::core::event::ButtonPressEvent(action.target, true));
            } else {
                held_ &= ~(uint64_t(1) << action.target);
                bus_.emit(oc::core::event::ButtonReleaseEvent(action.target));
            }
            break;
        case ActionKind::ENCODER_VALUE:
            bus_.emit(oc::core::event::EncoderChangedEvent(action.target, action.value / 1000.0f));
            break;
        case ActionKind::MARKER:
            marker_ = action.target; profiler::setMarker(marker_); break;
        case ActionKind::TRANSPORT:
            state_.statusBar.playing.set(action.value != 0); break;
        case ActionKind::BLOCK_FOREGROUND: {
#if defined(ARDUINO_TEENSY41)
            // Deliberate CPU stall, IRQs stay enabled: no delay()/yield(), no
            // transport polling. This path exists only in RAM-only benchmarks.
            const uint32_t outputs = metricSamples("midi.usb-queue-age");
            const uint32_t start = micros();
            while (micros() - start < uint32_t(action.value)) asm volatile("nop");
            OC_PERF_RECORD("benchmark.foreground-block", micros() - start,
                           uint32_t(action.value), metricSamples("midi.usb-queue-age") - outputs);
#else
            run_.fail("foreground_block_requires_hardware");
#endif
            break;
        }
        case ActionKind::ASSERT_VIEW: case ActionKind::ASSERT_MODE: break;
    }
    injecting_ = false;
    capture(trace);
    if (action.kind == ActionKind::ASSERT_VIEW && trace.view != action.value)
        run_.fail("view_assertion_failed");
    if (action.kind == ActionKind::ASSERT_MODE && trace.modeHash != uint32_t(action.value))
        run_.fail("mode_assertion_failed");
}

void HardwareBenchmarkEndpoint::finish(uint32_t nowUs) {
    endMetrics(); profiler::endRun();
    clearSyntheticButtons();
    injecting_ = true;
    for (uint16_t button = 0; button < 48; ++button)
        if (held_ & (uint64_t(1) << button)) bus_.emit(oc::core::event::ButtonReleaseEvent(button));
    held_ = 0;
    injecting_ = false;
    state_.statusBar.playing.set(false);
    cleanupAtUs_ = nowUs + 250'000U;
    finished_ = true;
}

void HardwareBenchmarkEndpoint::advance(uint32_t nowUs) {
    observeMidiInput();
    if (cancelRequested_) { run_.cancel(); cancelRequested_ = false; }
    if (run_.beginIfDue(nowUs)) {
        beginMetrics(run_.startedAtUs());
        if (!profiler::beginRun(run_.id())) run_.fail("lvgl_profiler_not_ready");
        if (oc::state::NotificationQueue::instance().hasOverflowed()) run_.fail("notification_overflow");
    }
    if (const auto* action = run_.takeDue(nowUs)) execute(*action, nowUs);
    if (run_.state() == RunState::RUNNING && oc::state::NotificationQueue::instance().hasOverflowed())
        run_.fail("notification_overflow");
    run_.completeIfDue(nowUs);
    if (run_.terminal() && !finished_) finish(nowUs);
    if (finished_ && !ready_ && static_cast<int32_t>(nowUs - cleanupAtUs_) >= 0) {
        memory_ = core::diagnostics::dynamicMemorySnapshot();
        lv_mem_monitor_t monitor{}; lv_mem_monitor(&monitor);
        lvglUsed_ = monitor.total_size - monitor.free_size; lvglLargest_ = monitor.free_biggest_size;
        lvglPeak_ = monitor.max_used;
#if defined(ARDUINO_TEENSY41)
        ram2Tail_ = reinterpret_cast<uintptr_t>(&_heap_end) - reinterpret_cast<uintptr_t>(__brkval);
#endif
        notificationOverflows_ = oc::state::NotificationQueue::instance().overflowCount();
        ready_ = true;
    }
#if defined(ARDUINO_TEENSY41)
    if (rebootAtUs_ && static_cast<int32_t>(nowUs - rebootAtUs_) >= 0) {
        SCB_AIRCR = 0x05FA0004;
        while (true) {} // Dedicated RAM-only firmware, after RPC acknowledgement and MIDI stop.
    }
#endif
}

void HardwareBenchmarkEndpoint::result(uint32_t runId, uint8_t section, uint16_t index,
                                       uint16_t request) {
    if (runId != run_.id()) { reply(request, RpcStatus::CONFLICT, "{}"); return; }
    if (!ready_) { reply(request, RpcStatus::NOT_READY, "{}"); return; }
    switch (section) {
        case 0:
            if (index) break;
            std::snprintf(json_, sizeof(json_),
                "{\"state\":\"%s\",\"run_id\":%lu,\"ok\":%s,\"failure\":\"%s\","
                "\"event_count\":%u,\"uploaded_count\":%u,\"metric_count\":%u,\"memory_count\":1,"
                "\"max_lateness_us\":%lu,\"foreign_requests\":%lu,\"notification_overflows\":%u,"
                "\"lvgl_errors\":%lu,\"duration_us\":%lu,\"boundary_skipped\":%lu,"
                "\"physical_inputs\":%lu,\"midi_inputs\":%lu,\"filesystem_requests\":%lu}", stateName(run_.state()),
                (unsigned long)run_.id(), run_.state() == RunState::COMPLETED && !foreignRequests_ &&
                !physicalInputs_ && !midiInputs_ && !notificationOverflows_ && !profiler::snapshot().errors &&
                !memory_.trackerOverflow ? "true" : "false",
                run_.failure(), unsigned(traceCount_), unsigned(run_.count()), unsigned(metricCount()),
                (unsigned long)run_.maxLatenessUs(), (unsigned long)foreignRequests_,
                unsigned(notificationOverflows_),
                (unsigned long)profiler::snapshot().errors, (unsigned long)run_.durationUs(),
                (unsigned long)boundarySkipped(), (unsigned long)physicalInputs_, (unsigned long)midiInputs_,
                (unsigned long)filesystemRequests_);
            reply(request, RpcStatus::OK, json_); return;
        case 1:
            if (index >= traceCount_) break;
            { const auto& t = trace_[index];
              std::snprintf(json_, sizeof(json_),
                "{\"index\":%u,\"due_us\":%lu,\"actual_us\":%lu,\"lateness_us\":%lu,"
                "\"marker\":%u,\"view\":%u,\"overlay\":%u,\"playing\":%s,\"mode\":\"%s\",\"mode_hash\":%lu}",
                unsigned(index), (unsigned long)t.dueUs, (unsigned long)t.actualUs,
                (unsigned long)t.latenessUs, unsigned(t.marker), unsigned(t.view), unsigned(t.overlay),
                t.playing ? "true" : "false", t.mode, (unsigned long)t.modeHash);
              reply(request, RpcStatus::OK, json_); return; }
        case 2:
            if (index >= metricCount()) break;
            { const auto& m = metric(index);
              size_t length = std::snprintf(json_, sizeof(json_),
                "{\"name\":\"%s\",\"count\":%lu,\"total_us\":%s,\"max_us\":%lu,"
                "\"max_at_us\":%lu,\"unit_a_max\":%lu,\"unit_b_max\":%lu,\"bins_log2\":[", m.name,
                (unsigned long)m.count, decimal64(m.totalUs).data(), (unsigned long)m.maxUs,
                (unsigned long)m.maxAtUs, (unsigned long)m.unitAMax, (unsigned long)m.unitBMax);
              for (size_t b = 0; b < m.bins.size(); ++b)
                  length += std::snprintf(json_ + length, sizeof(json_) - length, "%s%lu", b ? "," : "", (unsigned long)m.bins[b]);
              std::snprintf(json_ + length, sizeof(json_) - length, "]}");
              reply(request, RpcStatus::OK, json_); return; }
        case 3:
            if (index > 2) break;
            { const auto& s = profiler::snapshot();
              const auto& frame = index == 1 ? s.worst : s.last;
              const auto& phases = index == 0 ? s.totals : frame.phase;
              size_t length = std::snprintf(json_, sizeof(json_),
                "{\"index\":%u,\"frames\":%lu,\"total_handler_us\":%s,\"errors\":%lu,"
                "\"marker\":%lu,\"sequence\":%lu,\"started_at_us\":%lu,\"handler_us\":%lu,\"invalidated_pixels\":%lu,\"submitted_pixels\":%lu,\"phases\":[",
                unsigned(index), (unsigned long)s.frames, decimal64(s.totalHandlerUs).data(),
                (unsigned long)s.errors, (unsigned long)frame.marker, (unsigned long)frame.sequence,
                (unsigned long)(frame.startedAtUs - run_.startedAtUs()), (unsigned long)frame.handlerUs,
                (unsigned long)frame.invalidatedPixels, (unsigned long)frame.submittedPixels);
              static constexpr const char* names[] = {"timer", "refresh", "layout", "style", "draw", "flush"};
              for (size_t p = 0; p < phases.size(); ++p)
                  length += std::snprintf(json_ + length, sizeof(json_) - length,
                    "%s{\"name\":\"%s\",\"calls\":%lu,\"total_us\":%s,\"max_us\":%lu}",
                    p ? "," : "", names[p], (unsigned long)phases[p].calls,
                    decimal64(phases[p].totalUs).data(), (unsigned long)phases[p].maxUs);
              std::snprintf(json_ + length, sizeof(json_) - length, "]}");
              reply(request, RpcStatus::OK, json_); return; }
        case 4:
            if (index) break;
            std::snprintf(json_, sizeof(json_),
                "{\"phase\":\"after_cleanup\",\"psram_user_bytes\":%lu,\"psram_free_bytes\":%lu,"
                "\"psram_largest_bytes\":%lu,\"allocation_failures\":%lu,\"tracker_overflow\":%s,"
                "\"lvgl_used_bytes\":%lu,\"lvgl_largest_bytes\":%lu,\"ram2_tail_bytes\":%lu,"
                "\"psram_peak_user_bytes_since_boot\":%lu,\"psram_min_free_bytes_since_boot\":%lu,"
                "\"lvgl_peak_used_bytes_since_boot\":%lu,\"tracker_ready\":%s}",
                (unsigned long)memory_.psramUserBytes, (unsigned long)memory_.psramFreeBytes,
                (unsigned long)memory_.psramLargestBlock, (unsigned long)memory_.psramAllocationFailures,
                memory_.trackerOverflow ? "true" : "false", (unsigned long)lvglUsed_,
                (unsigned long)lvglLargest_, (unsigned long)ram2Tail_,
                (unsigned long)memory_.psramPeakUserBytes,
                (unsigned long)memory_.psramMinimumFreeBytes,
                (unsigned long)lvglPeak_, memory_.trackerReady ? "true" : "false");
            reply(request, RpcStatus::OK, json_); return;
    }
    reply(request, RpcStatus::INVALID_ARGUMENT, "{}");
}
} // namespace core::validation::benchmark
#endif

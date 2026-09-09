#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <lvgl.h>
#include <oc/core/event/EventBus.hpp>
#include <oc/core/event/Events.hpp>
#include <oc/diagnostics/Performance.hpp>
#include <oc/impl/MemoryStorage.hpp>
#include <oc/time/Time.hpp>
#include <oc/ui/lvgl/LvglFrameProfiler.hpp>
#include <oc/ui/lvgl/LvglProfilerHooks.h>

#include "config/InputIDs.hpp"
#include "persistence/DeviceSettingsStorageLayout.hpp"
#include "state/CoreState.hpp"
#include "support/NotificationTestUtils.hpp"
#include "validation/benchmark/BenchInput.hpp"
#include "validation/benchmark/HardwareBenchmarkEndpoint.hpp"
#include "validation/benchmark/HardwareBenchmarkFixture.hpp"
#include "validation/benchmark/HardwareBenchmarkMetrics.hpp"
#include "validation/ux/SemanticUxContext.hpp"

namespace bench = core::validation::benchmark;
namespace profiler = oc::ui::lvgl::benchmark;
namespace events = oc::core::event;

namespace {
uint32_t nowUs = 1'000;
uint32_t receivedMidi = 0;
uint32_t midiInputCount() { return receivedMidi; }
unsigned memoryReads = 0, lvglMemoryReads = 0;
constexpr uint16_t BUTTON = static_cast<uint16_t>(Config::ButtonID::MACRO_1);
constexpr uint16_t ENCODER = static_cast<uint16_t>(Config::EncoderID::NAV);

void put16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(value & 255U); bytes.push_back(value >> 8U);
}
void put32(std::vector<uint8_t>& bytes, uint32_t value) {
    put16(bytes, uint16_t(value)); put16(bytes, uint16_t(value >> 16U));
}
std::vector<uint8_t> idBody(uint32_t id) {
    std::vector<uint8_t> out; put32(out, id); return out;
}
std::vector<uint8_t> program(uint32_t id, const std::vector<bench::Action>& actions,
                             uint32_t duration = 1000, uint32_t lateBudget = 100) {
    auto bytes = idBody(id);
    put16(bytes, static_cast<uint16_t>(actions.size()));
    put32(bytes, duration); put32(bytes, lateBudget);
    for (const auto& action : actions) {
        put32(bytes, action.dueUs);
        bytes.push_back(uint8_t(action.kind)); bytes.push_back(0);
        put16(bytes, action.target); put32(bytes, uint32_t(action.value));
    }
    return bytes;
}
int32_t modeHash(const char* mode) {
    const uint32_t hash = bench::fnv1a(reinterpret_cast<const uint8_t*>(mode), std::strlen(mode));
    int32_t result; std::memcpy(&result, &hash, sizeof(result)); return result;
}
void contains(const std::string& text, const std::string& fragment) {
    if (text.find(fragment) == std::string::npos) {
        std::cerr << "Missing " << fragment << " in " << text << '\n';
        assert(false);
    }
}

class Transport final : public oc::interface::ITransport {
public:
    ReceiveCallback receive;
    std::vector<std::vector<uint8_t>> replies;
    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update() override {}
    void setOnReceive(ReceiveCallback callback) override { receive = std::move(callback); }
    void send(const uint8_t* data, size_t size) override {
        replies.emplace_back(data, data + size);
        assert(size >= 8 && size <= 3078);
        std::cout << "WIRE_JSON " << std::string(data + 6, data + size) << '\n';
    }
    std::string request(uint8_t operation, const std::vector<uint8_t>& body = {},
                        bench::RpcStatus expected = bench::RpcStatus::OK,
                        uint16_t requestId = 0xA1B2) {
        std::vector<uint8_t> bytes{bench::REQUEST_ID, 0, bench::SCHEMA};
        put16(bytes, requestId); bytes.push_back(operation);
        bytes.insert(bytes.end(), body.begin(), body.end());
        const size_t previous = replies.size();
        assert(receive); receive(bytes.data(), bytes.size());
        assert(replies.size() == previous + 1); // Exactly one framed response.
        const auto& reply = replies.back();
        assert(reply[0] == bench::RESPONSE_ID && reply[1] == 0 && reply[2] == bench::SCHEMA);
        assert(bench::read16(reply.data() + 3) == requestId);
        if (reply[5] != uint8_t(expected)) {
            std::cerr << "Operation " << unsigned(operation) << " at " << nowUs
                      << " expected status " << unsigned(expected) << ", got "
                      << unsigned(reply[5]) << '\n';
            assert(false);
        }
        return {reply.begin() + 6, reply.end()};
    }
};

class PhysicalButtons final : public oc::interface::IButton {
public:
    bool held = false;
    oc::type::Result<void> init() override { return oc::type::Result<void>::ok(); }
    void update(uint32_t) override {}
    bool isPressed(oc::type::ButtonID id) const override { return held && id == BUTTON; }
    void setCallback(oc::type::ButtonCallback) override {}
};

struct Harness {
    oc::impl::MemoryStorage storage{core::persistence::device_settings::layout::STORAGE_END};
    std::unique_ptr<core::state::CoreState> state = std::make_unique<core::state::CoreState>(storage);
    events::EventBus bus;
    Transport transport;
    PhysicalButtons* physical = nullptr;
    std::unique_ptr<bench::BenchButtons> buttons;
    std::unique_ptr<bench::HardwareBenchmarkEndpoint> endpoint;
    unsigned presses = 0, releases = 0, turns = 0;
    float encoderValue = 0;
    bool navigateOnPress = false;

    explicit Harness(bool monitorMidi = false) {
        nowUs = 1'000;
        receivedMidi = 0;
        memoryReads = lvglMemoryReads = 0;
        oc::time::setMicrosProvider([] { return nowUs; });
        oc::time::setProvider([] { return nowUs / 1000U; });
        oc::state::NotificationQueue::instance().resetOverflowCount();
        assert(bench::prepareHardwareBenchmarkFixture(*state));
        test_support::drainNotifications();
        auto source = std::make_unique<PhysicalButtons>();
        physical = source.get();
        buttons = std::make_unique<bench::BenchButtons>(std::move(source));
        assert(buttons->init());
        bus.on(oc::type::EventCategory::USER_INPUT, events::InputEvent::BUTTON_PRESS,
            [this](const oc::type::Event& value) {
                const auto& event = static_cast<const events::ButtonPressEvent&>(value);
                ++presses;
                // Exercise ButtonAPI's held-state contract before observers run.
                assert(buttons->isPressed(event.buttonId));
                if (navigateOnPress) state->activeView.set(core::ui::ViewType::MODULATORS);
            });
        bus.on(oc::type::EventCategory::USER_INPUT, events::InputEvent::BUTTON_RELEASE,
            [this](const oc::type::Event& value) {
                const auto& event = static_cast<const events::ButtonReleaseEvent&>(value);
                ++releases;
                assert(!buttons->isPressed(event.buttonId));
            });
        bus.on(oc::type::EventCategory::USER_INPUT, events::InputEvent::ENCODER_CHANGED,
            [this](const oc::type::Event& value) {
                ++turns;
                encoderValue = static_cast<const events::EncoderChangedEvent&>(value).normalizedValue;
            });
        endpoint = std::make_unique<bench::HardwareBenchmarkEndpoint>(
            transport, bus, *state, monitorMidi ? &midiInputCount : nullptr);
        endpoint->begin();
        assert(bus.getSubscriberCount() == 6);
    }
    ~Harness() {
        endpoint.reset();
        assert(!transport.receive && bus.getSubscriberCount() == 3);
        test_support::drainNotifications();
    }
    void advance(uint32_t at) {
        nowUs = at; endpoint->advance(at); test_support::drainNotifications();
    }
    void physicalInput(unsigned kind) {
        if (kind == 0) {
            physical->held = true;
            bus.emit(events::ButtonPressEvent(BUTTON, true));
        } else if (kind == 1) {
            physical->held = false;
            bus.emit(events::ButtonReleaseEvent(BUTTON));
        } else {
            bus.emit(events::EncoderChangedEvent(ENCODER, 0.5f));
        }
    }
    uint32_t start(uint32_t id, const std::vector<bench::Action>& actions = {},
                   uint32_t duration = 1000, uint32_t lateBudget = 100) {
        transport.request(1, program(id, actions, duration, lateBudget));
        transport.request(2, idBody(id));
        return nowUs + bench::ARM_DELAY_US;
    }
    std::string result(uint32_t id, uint8_t section = 0, uint16_t index = 0,
                       bench::RpcStatus status = bench::RpcStatus::OK) {
        auto body = idBody(id); body.push_back(section); put16(body, index);
        return transport.request(4, body, status);
    }
};

class ModeProvider final : public core::validation::ux::SemanticUxContextProvider {
public:
    core::state::CoreState& state;
    const char* forced = nullptr;
    mutable unsigned calls = 0;
    explicit ModeProvider(core::state::CoreState& source) : state(source) {
        core::validation::ux::setCurrentSemanticUxContextProvider(this);
    }
    ~ModeProvider() { core::validation::ux::clearCurrentSemanticUxContextProvider(this); }
    void captureSemanticUxContext(const oc::core::input::InputBindingTraceEvent& event,
                                 core::validation::ux::SemanticUxContext& out) const override {
        ++calls;
        assert(event.buttonId == std::numeric_limits<oc::type::ButtonID>::max());
        assert(event.encoderId == std::numeric_limits<oc::type::EncoderID>::max());
        out.mode = forced ? forced : state.activeView.get() == core::ui::ViewType::MODULATORS
            ? "modulators.source.workspace" : nullptr;
    }
};

void cancelledBeforeAnyCapture() {
    Harness h;
    h.transport.request(1, program(10, {}));
    h.transport.request(5, idBody(10)); h.advance(nowUs);
    h.result(10, 0, 0, bench::RpcStatus::NOT_READY);
    h.advance(nowUs + 250000);
    contains(h.result(10), "\"metric_count\":0");
    h.result(10, 2, 0, bench::RpcStatus::INVALID_ARGUMENT);
    contains(h.result(10, 3, 0), "\"frames\":0");
    assert(!profiler::snapshot().active);
    std::cout << "[PASS] cold cancellation has no uninitialized metrics or early results\n";
}

void protocolAndStart() {
    Harness h;
    contains(h.transport.request(0), "\"build_id\":\"native-endpoint-contract\"");
    contains(h.transport.request(3), "\"state\":\"idle\"");
    for (auto malformed : std::vector<std::vector<uint8_t>>{
             {}, {0xDE}, {0xDE, 0, 1, 1, 0}, {0xDD, 0, 1, 1, 0, 0},
             {0xDE, 1, 1, 1, 0, 0}, {0xDE, 0, 2, 1, 0, 0}}) {
        const size_t before = h.transport.replies.size();
        h.transport.receive(malformed.data(), malformed.size());
        assert(h.transport.replies.size() == before);
    }
    h.transport.request(99, {}, bench::RpcStatus::UNSUPPORTED);
    for (uint8_t op : {0, 1, 2, 3, 4, 5, 6})
        h.transport.request(op, {0}, bench::RpcStatus::INVALID_ARGUMENT);
    h.transport.request(2, idBody(11), bench::RpcStatus::CONFLICT);
    const auto bytes = program(11, {{0, bench::ActionKind::MARKER, 7, 0}});
    const auto first = h.transport.request(1, bytes);
    assert(h.transport.request(1, bytes) == first);
    auto changed = bytes; changed.back() = 1; // Invalid marker value cannot mutate upload.
    h.transport.request(1, changed, bench::RpcStatus::INVALID_ARGUMENT);
    changed = bytes; changed[0] = 12;
    h.transport.request(1, changed, bench::RpcStatus::CONFLICT);
    h.physical->held = true;
    contains(h.transport.request(2, idBody(11), bench::RpcStatus::INVALID_STATE), "physical_button_held");
    h.physical->held = false;
    h.transport.request(2, idBody(12), bench::RpcStatus::CONFLICT);
    contains(h.transport.request(2, idBody(11)), "\"state\":\"armed\"");
    const uint32_t start = nowUs + bench::ARM_DELAY_US;
    nowUs += 100;
    h.transport.request(2, idBody(11)); // Retry must not move the deadline.
    assert(h.result(11, 0, 0, bench::RpcStatus::NOT_READY) == "{}");
    h.advance(start - 1); assert(!profiler::snapshot().active);
    h.advance(start); assert(profiler::snapshot().active && profiler::snapshot().marker == 7);
    h.advance(start + 1000);
    assert(h.result(11, 0, 0, bench::RpcStatus::NOT_READY) == "{}");
    h.advance(start + 251000);
    contains(h.result(11), "\"ok\":true");
    h.transport.request(1, bytes, bench::RpcStatus::INVALID_STATE);
    h.transport.request(2, idBody(11)); h.advance(start + 500000);
    assert(!profiler::snapshot().active); // One run per endpoint/boot.
    contains(h.result(11), "\"event_count\":1");
    std::cout << "[PASS] framing, malformed bodies, exact upload retry, held start, one run per boot\n";
}

void cleanRunAndResults() {
    Harness h; ModeProvider context(*h.state); h.navigateOnPress = true;
    using K = bench::ActionKind;
    const uint32_t start = h.start(21, {
        {0, K::ASSERT_MODE, 0, modeHash("macro.performance")}, {0, K::MARKER, 7, 0},
        {100, K::TRANSPORT, 0, 1}, {200, K::BUTTON, BUTTON, 1},
        {300, K::ENCODER_VALUE, ENCODER, -1250}, {400, K::BUTTON, BUTTON, 0},
        {500, K::ASSERT_VIEW, 0, int32_t(core::ui::ViewType::MODULATORS)},
        {600, K::ASSERT_MODE, 0, modeHash("modulators.source.workspace")}});
    const size_t replies = h.transport.replies.size();
    h.advance(start); assert(context.calls == 1 && profiler::snapshot().marker == 0);
    h.advance(start); assert(context.calls == 2 && profiler::snapshot().marker == 7);
    h.advance(start + 100); assert(h.state->statusBar.playing.get());
    for (uint32_t due : {200, 300, 400, 500, 600}) h.advance(start + due);
    assert(context.calls == 8 && h.presses == 1 && h.releases == 1 && h.turns == 1);
    assert(h.encoderValue == -1.25f);
    nowUs = start + 700;
    assert(profiler::beginFrame());
    oc_lvgl_profile_begin(static_cast<unsigned>(profiler::Phase::Layout));
    nowUs += 12; oc_lvgl_profile_end(static_cast<unsigned>(profiler::Phase::Layout));
    nowUs += 8; profiler::endFrame(100, 80);
    nowUs = start + 800;
    oc::diagnostics::recordPerformance({"main.loop", 12, 3, 4});
    h.advance(start + 1000);
    assert(!h.state->statusBar.playing.get() && !profiler::snapshot().active);
    assert(h.transport.replies.size() == replies && memoryReads == 0 && lvglMemoryReads == 0);
    oc::diagnostics::recordPerformance({"main.loop", 999, 999, 999});
    contains(h.transport.request(3), "\"state\":\"finishing\"");
    h.result(21, 4, 0, bench::RpcStatus::NOT_READY);
    h.advance(start + 250999); assert(memoryReads == 0 && lvglMemoryReads == 0);
    h.advance(start + 251000); assert(memoryReads == 1 && lvglMemoryReads == 1);
    const auto summary = h.result(21);
    contains(summary, "\"state\":\"completed\""); contains(summary, "\"ok\":true");
    contains(summary, "\"event_count\":8"); contains(summary, "\"foreign_requests\":0");
    contains(summary, "\"physical_inputs\":0,\"midi_inputs\":0");
    assert(h.result(21) == summary);
    contains(h.result(21, 1, 0), "\"mode\":\"macro.performance\"");
    const auto trace = h.result(21, 1, 7);
    contains(trace, "\"mode\":\"modulators.source.workspace\"");
    contains(trace, "\"marker\":7"); contains(trace, "\"actual_us\":600");
    contains(trace, "\"playing\":true");
    uint16_t metricIndex = 0;
    for (; metricIndex < bench::metricCount(); ++metricIndex)
        if (std::strcmp(bench::metric(metricIndex).name, "main.loop") == 0) break;
    assert(metricIndex < bench::metricCount());
    const auto metric = h.result(21, 2, metricIndex);
    contains(metric, "\"name\":\"main.loop\""); contains(metric, "\"count\":1");
    contains(metric, "\"total_us\":12"); contains(metric, "\"unit_a_max\":3");
    for (uint16_t index = 0; index < bench::metricCount(); ++index) h.result(21, 2, index);
    for (uint16_t index = 0; index < 3; ++index) {
        const auto frame = h.result(21, 3, index);
        contains(frame, "\"frames\":1"); contains(frame, "\"handler_us\":20");
        contains(frame, "\"name\":\"layout\",\"calls\":1,\"total_us\":12,\"max_us\":12");
    }
    const auto memory = h.result(21, 4);
    contains(memory, "\"phase\":\"after_cleanup\"");
    contains(memory, "\"psram_user_bytes\":123"); contains(memory, "\"lvgl_used_bytes\":600");
    contains(memory, "\"psram_peak_user_bytes_since_boot\":234");
    contains(memory, "\"psram_min_free_bytes_since_boot\":345");
    contains(memory, "\"lvgl_peak_used_bytes_since_boot\":800");
    contains(memory, "\"tracker_ready\":true");
    h.result(22, 0, 0, bench::RpcStatus::CONFLICT);
    for (auto bounds : std::vector<std::pair<uint8_t, uint16_t>>{{0,1},{1,8},{2,uint16_t(bench::metricCount())},{3,3},{4,1},{5,0}})
        h.result(21, bounds.first, bounds.second, bench::RpcStatus::INVALID_ARGUMENT);
    assert(memoryReads == 1 && lvglMemoryReads == 1);
    std::cout << "[PASS] one action per turn, real bus/state, silent capture, stable paged traces/metrics/LVGL/memory\n";
}

void fullWidthTotals() {
    Harness h;
    const uint32_t start = h.start(61, {}, 120'000'000);
    h.advance(start);
    nowUs = start + 80'000'000;
    // Queue-age samples overlap: their real cumulative sum may exceed run time.
    for (unsigned sample = 0; sample < 75; ++sample)
        oc::diagnostics::recordPerformance({"midi.usb-queue-age", 60'000'000,
                                            123'456'789, 987'654'321});
    h.advance(start + 120'000'000);
    h.advance(start + 120'250'000);
    uint16_t index = 0;
    for (; index < bench::metricCount(); ++index)
        if (std::strcmp(bench::metric(index).name, "midi.usb-queue-age") == 0) break;
    assert(index < bench::metricCount());
    contains(h.result(61, 2, index),
        "\"count\":75,\"total_us\":4500000000,\"max_us\":60000000,"
        "\"max_at_us\":80000000,\"unit_a_max\":123456789,\"unit_b_max\":987654321");

    // Serializer-only boundary fixture: the frozen collector is backed by a
    // mutable object. These extreme LVGL totals are not a possible 120 s run.
    // Avoid adding a production setter solely to exercise all 20 decimal digits.
    auto& snapshot = const_cast<profiler::Snapshot&>(profiler::snapshot());
    assert(!snapshot.active);
    snapshot.frames = 37;
    snapshot.totalHandlerUs = UINT64_MAX;
    snapshot.errors = 5;
    snapshot.last.marker = 19;
    snapshot.last.sequence = 23;
    snapshot.last.startedAtUs = start + 123;
    snapshot.last.handlerUs = 29;
    snapshot.last.invalidatedPixels = 31;
    snapshot.last.submittedPixels = 41;
    for (auto& phase : snapshot.last.phase) phase = {43, UINT64_MAX, 47};
    snapshot.worst = snapshot.last;
    snapshot.totals = snapshot.last.phase;
    for (uint16_t page = 0; page < 3; ++page) {
        const auto frame = h.result(61, 3, page);
        contains(frame,
            "\"frames\":37,\"total_handler_us\":18446744073709551615,\"errors\":5,"
            "\"marker\":19,\"sequence\":23,\"started_at_us\":123,\"handler_us\":29,"
            "\"invalidated_pixels\":31,\"submitted_pixels\":41");
        contains(frame, "\"calls\":43,\"total_us\":18446744073709551615,\"max_us\":47");
        assert(h.result(61, 3, page) == frame);
    }
    std::cout << "[PASS] 64-bit metric/LVGL totals and subsequent format arguments remain exact\n";
}

void cancelAndReset() {
    using K = bench::ActionKind;
    for (bool reset : {false, true}) {
        Harness h;
        const uint32_t start = h.start(31, {{0,K::BUTTON,BUTTON,1},{1000,K::BUTTON,BUTTON,0}}, 2000);
        h.advance(start); assert(h.buttons->isPressed(BUTTON));
        h.state->statusBar.playing.set(true);
        h.transport.request(5, idBody(32), bench::RpcStatus::CONFLICT);
        if (reset) contains(h.transport.request(6), "\"rebooting\":true");
        else contains(h.transport.request(5, idBody(31)), "\"cancel_requested\":true");
        assert(h.buttons->isPressed(BUTTON)); // Acknowledge first; cleanup is foreground work.
        h.advance(start + 1);
        assert(!h.buttons->isPressed(BUTTON) && h.releases == 1 && !h.state->statusBar.playing.get());
        assert(!profiler::snapshot().active);
        h.result(31, 0, 0, bench::RpcStatus::NOT_READY);
        h.advance(start + 250001);
        const auto summary = h.result(31);
        contains(summary, "\"state\":\"cancelled\""); contains(summary, "\"ok\":false");
        contains(summary, "\"event_count\":1");
        contains(summary, "\"physical_inputs\":0,\"midi_inputs\":0");
        h.advance(start + 500001); // Native reset acknowledgement cannot reboot the host.
        assert(h.releases == 1 && h.result(31) == summary);
        h.transport.request(2, idBody(31), bench::RpcStatus::INVALID_STATE);
    }
    for (bool arm : {false, true}) {
        Harness h;
        h.transport.request(1, program(32, {}));
        if (arm) h.transport.request(2, idBody(32));
        h.transport.request(5, idBody(32)); h.advance(nowUs);
        h.advance(nowUs + 250000);
        contains(h.result(32), "\"event_count\":0");
        contains(h.result(32), "\"state\":\"cancelled\"");
    }
    std::cout << "[PASS] cancel/reset acknowledged before cleanup; synthetic holds released exactly once\n";
}

void contaminationAndAssertions() {
    using K = bench::ActionKind;
    {
        Harness h; const auto start = h.start(41);
        h.advance(start);
        assert(h.result(41, 2, 0, bench::RpcStatus::NOT_READY) == "{}");
        const size_t replies = h.transport.replies.size();
        const uint8_t foreign[] = {0x02, 0x00, 0x01, 0x01, 0x00, 0x00};
        h.transport.receive(foreign, sizeof(foreign));
        h.transport.receive(foreign, 1); // Even malformed/other protocol traffic contaminates.
        assert(h.transport.replies.size() == replies);
        assert(memoryReads == 0 && lvglMemoryReads == 0);
        h.advance(start + 1000); h.advance(start + 251000);
        const auto summary = h.result(41);
        contains(summary, "\"ok\":false"); contains(summary, "\"foreign_requests\":3");
    }
    for (bool duringRun : {false, true}) {
        for (unsigned input = 0; input < 3; ++input) {
            Harness h; const auto start = h.start(42);
            if (duringRun) h.advance(start);
            h.physicalInput(input);
            h.advance(duringRun ? start : nowUs);
            h.advance(nowUs + 250000);
            const auto summary = h.result(42);
            contains(summary, "\"ok\":false"); contains(summary, "physical_input_contamination");
        }
    }
    for (auto action : std::vector<bench::Action>{{0,K::ASSERT_VIEW,0,2},
             {0,K::ASSERT_MODE,0,modeHash("unexpected.mode")}}) {
        Harness h; const auto start = h.start(43, {action});
        h.advance(start); h.advance(start + 250000);
        contains(h.result(43), action.kind == K::ASSERT_VIEW ? "view_assertion_failed" : "mode_assertion_failed");
    }
    {
        Harness h; const auto start = h.start(44, {{0,K::MARKER,1,0}}, 1000, 10);
        h.advance(start + 11); h.advance(start + 250011);
        contains(h.result(44), "input_deadline_exceeded");
        contains(h.result(44), "\"event_count\":0");
    }
    {
        Harness h; ModeProvider context(*h.state); context.forced = "quote\"slash\\line\n";
        const auto start = h.start(45, {{0,K::ASSERT_MODE,0,modeHash(context.forced)}});
        h.advance(start); h.advance(start + 1000); h.advance(start + 251000);
        contains(h.result(45), "\"ok\":true");
        contains(h.result(45,1), "\"mode\":\"quote_slash_line_\"");
    }
    {
        Harness h; ModeProvider context(*h.state); const std::string largeMode(64, 'm');
        context.forced = largeMode.c_str();
        const auto start = h.start(46, {{0,K::MARKER,1,0}});
        h.advance(start); h.advance(start + 250000);
        contains(h.result(46), "semantic_mode_too_long"); h.result(46,1);
    }
    std::cout << "[PASS] early result denied, traffic/physical contamination, semantic assertions, deadlines, JSON safety\n";
}

void contaminationLifecycle() {
    for (bool uploaded : {false, true}) {
        for (unsigned input = 0; input < 3; ++input) {
            Harness h;
            if (uploaded) h.transport.request(1, program(71, {}));
            h.physicalInput(input);
            contains(h.transport.request(2, idBody(71), bench::RpcStatus::INVALID_STATE),
                     "physical_input_contamination");
            h.transport.request(1, program(71, {}), bench::RpcStatus::INVALID_STATE);
            h.advance(nowUs); h.advance(nowUs + 250000);
            const auto summary = h.result(uploaded ? 71 : 0);
            contains(summary, "\"ok\":false"); contains(summary, "physical_input_contamination");
            contains(summary, "\"physical_inputs\":1,\"midi_inputs\":0");
            contains(summary, "\"event_count\":0");
        }
    }
    // Receive must check MIDI itself: START can precede the next advance().
    for (bool uploaded : {false, true}) {
        Harness h(true);
        if (uploaded) h.transport.request(1, program(72, {}));
        receivedMidi = 4;
        contains(h.transport.request(2, idBody(72), bench::RpcStatus::INVALID_STATE),
                 "midi_input_contamination");
        h.transport.request(1, program(72, {}), bench::RpcStatus::INVALID_STATE);
        h.advance(nowUs); h.advance(nowUs + 250000);
        const auto summary = h.result(uploaded ? 72 : 0);
        contains(summary, "\"ok\":false"); contains(summary, "midi_input_contamination");
        contains(summary, "\"physical_inputs\":0,\"midi_inputs\":4");
        contains(summary, "\"event_count\":0");
    }
    for (bool running : {false, true}) {
        Harness h(true); const auto start = h.start(73);
        if (running) h.advance(start);
        receivedMidi = 9;
        const uint32_t failureAt = running ? start + 1 : start - 1;
        h.advance(failureAt); h.advance(failureAt + 250000);
        const auto summary = h.result(73);
        contains(summary, "\"ok\":false"); contains(summary, "midi_input_contamination");
        contains(summary, "\"physical_inputs\":0,\"midi_inputs\":9");
        assert(!profiler::snapshot().active);
    }
    for (unsigned input = 0; input < 4; ++input) {
        Harness h(true); const auto start = h.start(74);
        h.advance(start); h.advance(start + 1000); // Already terminal, not yet ready.
        if (input == 3) receivedMidi = 11;
        else h.physicalInput(input);
        h.advance(start + 251000);
        const auto summary = h.result(74);
        contains(summary, "\"state\":\"completed\""); contains(summary, "\"ok\":false");
        contains(summary, input == 3 ? "\"physical_inputs\":0,\"midi_inputs\":11"
                                     : "\"physical_inputs\":1,\"midi_inputs\":0");
        h.physicalInput(2); receivedMidi = UINT32_MAX;
        h.advance(start + 252000);
        assert(h.result(74) == summary); // Nothing after ready changes frozen counters.
    }
    for (bool beforeReady : {false, true}) {
        Harness h; const auto start = h.start(75);
        h.advance(start); h.advance(start + 1000);
        if (!beforeReady) h.advance(start + 251000);
        std::array<int, oc::MAX_PENDING_NOTIFICATIONS + 1> owners{};
        auto& queue = oc::state::NotificationQueue::instance();
        for (auto& owner : owners) queue.enqueue({&owner, 0}, &owner, [](void*, size_t) {});
        assert(queue.overflowCount() == 1);
        h.advance(start + 251000);
        const auto summary = h.result(75);
        contains(summary, beforeReady ? "\"notification_overflows\":1" : "\"notification_overflows\":0");
        contains(summary, beforeReady ? "\"ok\":false" : "\"ok\":true");
        queue.resetOverflowCount();
        assert(h.result(75) == summary);
    }
    std::cout << "[PASS] physical/MIDI contamination from idle through cleanup; all result counters freeze at ready\n";
}

void startupFailuresAndWrap() {
    {
        Harness h; const auto start = h.start(51);
        assert(profiler::beginRun(999) && profiler::beginFrame());
        h.advance(start);
        h.advance(start + 250000);
        contains(h.result(51), "lvgl_profiler_not_ready");
        assert(!profiler::snapshot().active);
    }
    {
        Harness h; const auto start = h.start(52);
        std::array<int, oc::MAX_PENDING_NOTIFICATIONS + 1> owners{};
        auto& queue = oc::state::NotificationQueue::instance();
        for (auto& owner : owners) queue.enqueue({&owner, 0}, &owner, [](void*, size_t) {});
        assert(queue.hasOverflowed());
        h.advance(start); h.advance(start + 250000);
        contains(h.result(52), "notification_overflow");
        contains(h.result(52), "\"ok\":false");
    }
    {
        Harness h; nowUs = UINT32_MAX - 100000U;
        const auto start = h.start(53, {{0,bench::ActionKind::TRANSPORT,0,1}});
        h.advance(start - 1); assert(!h.state->statusBar.playing.get());
        h.advance(start); assert(h.state->statusBar.playing.get());
        h.advance(start + 1000); h.advance(start + 251000);
        contains(h.result(53), "\"ok\":true");
    }
    std::cout << "[PASS] busy profiler/notification overflow fail closed; arm deadline wraps safely\n";
}

void foregroundBlockRequiresHardware() {
    Harness h;
    const auto start = h.start(81, {{0, bench::ActionKind::BLOCK_FOREGROUND, 0, 1000}}, 2000);
    h.advance(start);
    assert(nowUs == start); // Never busy-wait against the native fake clock.
    h.advance(start + 250000);
    contains(h.result(81), "foreground_block_requires_hardware");
    contains(h.result(81), "\"ok\":false");
    assert(bench::metricSamples("benchmark.foreground-block") == 0);
    std::cout << "[PASS] foreground stalls cannot produce simulated hardware evidence\n";
}

void filesystemSharesTransportWithoutHidingControlTraffic() {
    Harness h;
    unsigned delivered = 0;
    h.endpoint->setOnReceive([&](const uint8_t* bytes, size_t size) {
        assert(size == 2 && bytes[0] == 0xFC); ++delivered;
    });
    contains(h.transport.request(0), "\"ram_only\":false");
    const auto start = h.start(82);
    h.advance(start);
    const uint8_t bytes[] = {0xFC, 6};
    h.transport.receive(bytes, sizeof(bytes));
    assert(delivered == 1);
    h.advance(start + 1000); h.advance(start + 251000);
    const auto result = h.result(82);
    contains(result, "\"ok\":true");
    contains(result, "\"filesystem_requests\":1");
    contains(result, "\"foreign_requests\":0");
    h.endpoint->setOnReceive(nullptr);
    h.transport.receive(bytes, sizeof(bytes));
    assert(delivered == 1);
    contains(h.transport.request(0), "\"ram_only\":true");
}
} // namespace

void lv_mem_monitor(lv_mem_monitor_t* monitor) {
    ++lvglMemoryReads; *monitor = {1000, 400, 300, 800};
}
namespace core::diagnostics {
DynamicMemorySnapshot dynamicMemorySnapshot() {
    ++memoryReads;
    DynamicMemorySnapshot snapshot;
    snapshot.psramUserBytes = 123; snapshot.psramFreeBytes = 456;
    snapshot.psramLargestBlock = 321; snapshot.trackerReady = true;
    snapshot.psramPeakUserBytes = 234; snapshot.psramMinimumFreeBytes = 345;
    return snapshot;
}
} // namespace core::diagnostics

int main() {
    cancelledBeforeAnyCapture();
    protocolAndStart();
    cleanRunAndResults();
    fullWidthTotals();
    cancelAndReset();
    contaminationAndAssertions();
    contaminationLifecycle();
    startupFailuresAndWrap();
    foregroundBlockRequiresHardware();
    filesystemSharesTransportWithoutHidingControlTraffic();
    std::cout << "[PASS] HardwareBenchmarkEndpoint native contract (no hardware or filesystem)\n";
}

#pragma once

#if defined(MS_HARDWARE_BENCHMARK)
#include "validation/benchmark/HardwareBenchmarkRun.hpp"
#include "diagnostics/MemoryFootprintReporter.hpp"
#include <oc/interface/IEventBus.hpp>
#include <oc/interface/ITransport.hpp>

namespace core::state { struct CoreState; }
namespace core::validation::benchmark {

// PSRAM-owned by StandaloneContext. Control traffic runs outside the measured
// interval. This endpoint is never compiled into normal/product profiles.
class HardwareBenchmarkEndpoint {
public:
    HardwareBenchmarkEndpoint(oc::interface::ITransport&, oc::interface::IEventBus&,
                              core::state::CoreState&, uint32_t (*midiInputCount)() = nullptr);
    ~HardwareBenchmarkEndpoint();
    void begin();
    void advance(uint32_t nowUs);

private:
    struct Trace {
        uint32_t dueUs = 0, actualUs = 0, latenessUs = 0, modeHash = 0;
        uint16_t marker = 0;
        uint8_t view = 0, overlay = 0;
        bool playing = false;
        char mode[64]{};
    };
    void receive(const uint8_t*, size_t);
    void reply(uint16_t request, RpcStatus status, const char* json);
    void describe();
    void result(uint32_t runId, uint8_t section, uint16_t index, uint16_t request);
    void execute(const Action&, uint32_t nowUs);
    void capture(Trace&);
    void finish(uint32_t nowUs);
    void observeMidiInput();

    oc::interface::ITransport& transport_;
    oc::interface::IEventBus& bus_;
    core::state::CoreState& state_;
    uint32_t (*midiInputCount_)() = nullptr;
    HardwareBenchmarkRun run_;
    std::array<Trace, MAX_EVENTS> trace_{};
    core::diagnostics::DynamicMemorySnapshot memory_{};
    uint64_t held_ = 0;
    uint32_t cleanupAtUs_ = 0, rebootAtUs_ = 0, foreignRequests_ = 0;
    uint32_t physicalInputs_ = 0, midiInputs_ = 0;
    size_t notificationOverflows_ = 0;
    uint32_t lvglUsed_ = 0, lvglLargest_ = 0, ram2Tail_ = 0;
    uint16_t traceCount_ = 0, marker_ = 0;
    bool finished_ = false, ready_ = false, cancelRequested_ = false;
    bool injecting_ = false;
    std::array<oc::interface::SubscriptionID, 3> inputSubscriptions_{};
    char json_[3072]{};
};
} // namespace core::validation::benchmark
#endif

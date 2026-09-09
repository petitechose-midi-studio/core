#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace core::validation::benchmark {

inline constexpr uint8_t REQUEST_ID = 0xDE;
inline constexpr uint8_t RESPONSE_ID = 0xDF;
inline constexpr uint8_t SCHEMA = 1;
inline constexpr uint16_t MAX_EVENTS = 2048;
inline constexpr uint32_t MAX_DURATION_US = 120'000'000;
inline constexpr uint32_t ARM_DELAY_US = 500'000;
inline constexpr size_t EVENT_BYTES = 12;

enum class ActionKind : uint8_t {
    BUTTON = 1, ENCODER_VALUE, MARKER, ASSERT_VIEW, ASSERT_MODE, TRANSPORT,
    BLOCK_FOREGROUND
};
enum class RunState : uint8_t { IDLE, UPLOADED, ARMED, RUNNING, COMPLETED, CANCELLED, FAILED };
enum class RpcStatus : uint8_t { OK, INVALID_ARGUMENT, INVALID_STATE, NOT_READY, TOO_LARGE, CONFLICT, UNSUPPORTED };

struct Action {
    uint32_t dueUs = 0;
    ActionKind kind = ActionKind::MARKER;
    uint16_t target = 0;
    int32_t value = 0;
};

uint16_t read16(const uint8_t* bytes);
uint32_t read32(const uint8_t* bytes);
uint32_t fnv1a(const uint8_t* bytes, size_t size);
const char* stateName(RunState state);
bool validButton(uint16_t id);
bool validEncoder(uint16_t id);

// One fixed, prevalidated program per boot. No allocations, adaptive timing,
// transport, UI or HAL work here. The foreground executes at most one action
// per turn; backlog is observable and eventually fails, never silently dilated.
class HardwareBenchmarkRun {
public:
    RpcStatus upload(const uint8_t* body, size_t size);
    RpcStatus start(uint32_t runId, uint32_t nowUs);
    bool beginIfDue(uint32_t nowUs);
    const Action* takeDue(uint32_t nowUs);
    bool completeIfDue(uint32_t nowUs);
    void fail(const char* reason);
    void cancel();

    RunState state() const { return state_; }
    bool terminal() const;
    uint32_t id() const { return id_; }
    uint32_t hash() const { return hash_; }
    uint16_t count() const { return count_; }
    uint16_t consumed() const { return cursor_; }
    uint32_t durationUs() const { return durationUs_; }
    uint32_t startedAtUs() const { return startedAtUs_; }
    uint32_t latenessUs() const { return latenessUs_; }
    uint32_t maxLatenessUs() const { return maxLatenessUs_; }
    const char* failure() const { return failure_; }
    uint32_t elapsedUs(uint32_t nowUs) const { return nowUs - startedAtUs_; }

private:
    std::array<Action, MAX_EVENTS> actions_{};
    RunState state_ = RunState::IDLE;
    uint32_t id_ = 0, hash_ = 0, durationUs_ = 0, latenessBudgetUs_ = 0;
    uint32_t startedAtUs_ = 0, latenessUs_ = 0, maxLatenessUs_ = 0;
    uint16_t count_ = 0, cursor_ = 0;
    const char* failure_ = "";
};

}  // namespace core::validation::benchmark

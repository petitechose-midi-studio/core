#include "validation/benchmark/HardwareBenchmarkRun.hpp"

#include <algorithm>
#include <cstring>
#include <config/InputIDs.hpp>

namespace core::validation::benchmark {

uint16_t read16(const uint8_t* p) { return uint16_t(p[0]) | (uint16_t(p[1]) << 8); }
uint32_t read32(const uint8_t* p) { return uint32_t(read16(p)) | (uint32_t(read16(p + 2)) << 16); }
uint32_t fnv1a(const uint8_t* p, size_t size) {
    uint32_t hash = 2166136261U;
    for (size_t i = 0; i < size; ++i) hash = (hash ^ p[i]) * 16777619U;
    return hash;
}

const char* stateName(RunState state) {
    switch (state) {
        case RunState::IDLE: return "idle";
        case RunState::UPLOADED: return "uploaded";
        case RunState::ARMED: return "armed";
        case RunState::RUNNING: return "running";
        case RunState::COMPLETED: return "completed";
        case RunState::CANCELLED: return "cancelled";
        case RunState::FAILED: return "failed";
    }
    return "failed";
}

bool validButton(uint16_t id) {
    using Config::ButtonID;
    uint8_t index;
    switch (static_cast<ButtonID>(id)) {
        case ButtonID::LEFT_TOP: case ButtonID::LEFT_CENTER: case ButtonID::LEFT_BOTTOM:
        case ButtonID::BOTTOM_LEFT: case ButtonID::BOTTOM_CENTER: case ButtonID::BOTTOM_RIGHT:
        case ButtonID::NAV: return true;
        default: return Config::macroButtonIndex(id, index);
    }
}

bool validEncoder(uint16_t id) {
    uint8_t index;
    return id == static_cast<uint16_t>(Config::EncoderID::NAV) ||
           id == static_cast<uint16_t>(Config::EncoderID::OPT) ||
           Config::macroEncoderIndex(id, index);
}

namespace {
Action decode(const uint8_t* p) {
    const uint32_t bits = read32(p + 8);
    int32_t value;
    std::memcpy(&value, &bits, sizeof(value));
    return {read32(p), static_cast<ActionKind>(p[4]), read16(p + 6), value};
}

bool validAction(const Action& action, uint64_t& held) {
    switch (action.kind) {
        case ActionKind::BUTTON: {
            if (!validButton(action.target) || (action.value != 0 && action.value != 1)) return false;
            const uint64_t bit = uint64_t(1) << action.target;
            if (bool(held & bit) == bool(action.value)) return false;
            held ^= bit;
            return true;
        }
        case ActionKind::ENCODER_VALUE:
            return validEncoder(action.target); // finite signed milli-value; binding owns its bounds
        case ActionKind::MARKER:
            return action.value == 0;
        case ActionKind::ASSERT_VIEW:
            return action.target == 0 && action.value >= 0 && action.value < 5;
        case ActionKind::ASSERT_MODE:
            return action.target == 0; // FNV-1a of semantic mode, not an independent enum
        case ActionKind::TRANSPORT:
            return action.target == 0 && (action.value == 0 || action.value == 1);
        case ActionKind::BLOCK_FOREGROUND:
            return action.target == 0 && action.value > 0 && action.value <= 1'000'000;
    }
    return false;
}
}  // namespace

RpcStatus HardwareBenchmarkRun::upload(const uint8_t* body, size_t size) {
    if (!body || size < 14) return RpcStatus::INVALID_ARGUMENT;
    const uint32_t id = read32(body), duration = read32(body + 6), late = read32(body + 10);
    const uint16_t count = read16(body + 4);
    if (count > MAX_EVENTS) return RpcStatus::TOO_LARGE;
    if (id == 0 || duration == 0 || duration > MAX_DURATION_US || late > 1'000'000U ||
        size != 14U + count * EVENT_BYTES) return RpcStatus::INVALID_ARGUMENT;
    if (state_ != RunState::IDLE && state_ != RunState::UPLOADED) return RpcStatus::INVALID_STATE;
    uint64_t held = 0;
    uint32_t previous = 0;
    uint32_t blockedUntil = 0;
    for (uint16_t i = 0; i < count; ++i) {
        const uint8_t* bytes = body + 14 + i * EVENT_BYTES;
        const Action action = decode(bytes);
        if (bytes[5] != 0 || action.dueUs < previous || action.dueUs < blockedUntil || action.dueUs > duration ||
            !validAction(action, held)) return RpcStatus::INVALID_ARGUMENT;
        if (action.kind == ActionKind::BLOCK_FOREGROUND) {
            blockedUntil = action.dueUs + uint32_t(action.value);
            if (blockedUntil > duration) return RpcStatus::INVALID_ARGUMENT;
        }
        previous = action.dueUs;
        // Once loaded, only an exact retransmission is accepted. Hash alone
        // is insufficient for this idempotence/data-integrity boundary.
        if (state_ == RunState::UPLOADED) {
            const auto& old = actions_[i];
            if (old.dueUs != action.dueUs || old.kind != action.kind ||
                old.target != action.target || old.value != action.value) return RpcStatus::CONFLICT;
        }
    }
    if (held != 0) return RpcStatus::INVALID_ARGUMENT;
    if (state_ == RunState::UPLOADED) {
        return id == id_ && count == count_ && duration == durationUs_ && late == latenessBudgetUs_
            ? RpcStatus::OK : RpcStatus::CONFLICT;
    }
    for (uint16_t i = 0; i < count; ++i) actions_[i] = decode(body + 14 + i * EVENT_BYTES);
    id_ = id;
    hash_ = fnv1a(body + 14, size - 14);
    durationUs_ = duration;
    latenessBudgetUs_ = late;
    count_ = count;
    state_ = RunState::UPLOADED;
    return RpcStatus::OK;
}

RpcStatus HardwareBenchmarkRun::start(uint32_t runId, uint32_t nowUs) {
    if (runId == 0 || runId != id_) return RpcStatus::CONFLICT;
    if (state_ == RunState::IDLE || state_ == RunState::CANCELLED) return RpcStatus::INVALID_STATE;
    if (state_ != RunState::UPLOADED) return RpcStatus::OK; // retry cannot restart a run
    startedAtUs_ = nowUs + ARM_DELAY_US;
    state_ = RunState::ARMED;
    return RpcStatus::OK;
}

bool HardwareBenchmarkRun::beginIfDue(uint32_t nowUs) {
    if (state_ != RunState::ARMED || static_cast<int32_t>(nowUs - startedAtUs_) < 0) return false;
    state_ = RunState::RUNNING;
    return true;
}

const Action* HardwareBenchmarkRun::takeDue(uint32_t nowUs) {
    if (state_ != RunState::RUNNING || cursor_ == count_) return nullptr;
    const Action& action = actions_[cursor_];
    const uint32_t elapsed = elapsedUs(nowUs);
    if (elapsed < action.dueUs) return nullptr;
    latenessUs_ = elapsed - action.dueUs;
    maxLatenessUs_ = std::max(maxLatenessUs_, latenessUs_);
    if (latenessUs_ > latenessBudgetUs_) {
        fail("input_deadline_exceeded");
        return nullptr;
    }
    ++cursor_;
    return &action;
}

bool HardwareBenchmarkRun::completeIfDue(uint32_t nowUs) {
    if (state_ != RunState::RUNNING || cursor_ != count_ || elapsedUs(nowUs) < durationUs_) return false;
    state_ = RunState::COMPLETED;
    return true;
}

void HardwareBenchmarkRun::fail(const char* reason) {
    if (terminal()) return;
    failure_ = reason;
    state_ = RunState::FAILED;
}
void HardwareBenchmarkRun::cancel() {
    if (!terminal()) state_ = RunState::CANCELLED;
}
bool HardwareBenchmarkRun::terminal() const {
    return state_ == RunState::COMPLETED || state_ == RunState::FAILED || state_ == RunState::CANCELLED;
}

}  // namespace core::validation::benchmark

#pragma once

#include <cstdint>
#include <limits>

namespace sdl::integration {

class UxReplayTimeline {
public:
    [[nodiscard]] uint32_t schedule(uint32_t dueMs) const {
        if (!has_previous_) return dueMs;

        const uint32_t intervalMs = dueMs >= previous_due_ms_
            ? dueMs - previous_due_ms_
            : 0;
        const uint32_t intervalDeadline = saturatedAdd(previous_actual_ms_, intervalMs);
        return intervalDeadline > dueMs ? intervalDeadline : dueMs;
    }

    void record(uint32_t dueMs, uint32_t actualMs) {
        previous_due_ms_ = dueMs;
        previous_actual_ms_ = actualMs;
        has_previous_ = true;
    }

private:
    static uint32_t saturatedAdd(uint32_t lhs, uint32_t rhs) {
        const uint32_t limit = std::numeric_limits<uint32_t>::max();
        return rhs > limit - lhs ? limit : lhs + rhs;
    }

    uint32_t previous_due_ms_ = 0;
    uint32_t previous_actual_ms_ = 0;
    bool has_previous_ = false;
};

}  // namespace sdl::integration

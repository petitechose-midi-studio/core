#pragma once

#include <array>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace test_support {

enum class RealtimeWorkKind : uint8_t {
    QueueShift = 0,
    ResolverComparison,
    EngineStep,
    GraphVisit,
    CcDeadlineGroup,
    ClockCatchUp,
    Count,
};

/** Test/qualification-only bounded work ledger; never part of product state. */
struct RealtimeWorkCounters {
    static constexpr size_t KIND_COUNT =
        static_cast<size_t>(RealtimeWorkKind::Count);

    void add(RealtimeWorkKind kind, uint32_t increment = 1U) {
        const size_t index = static_cast<size_t>(kind);
        assert(index < values.size());
        values[index] = saturatingAdd(values[index], increment);
    }

    [[nodiscard]] uint32_t get(RealtimeWorkKind kind) const {
        const size_t index = static_cast<size_t>(kind);
        assert(index < values.size());
        return values[index];
    }

    void reset() {
        values.fill(0U);
    }

    [[nodiscard]] static constexpr uint32_t saturatingAdd(
        uint32_t value,
        uint32_t increment
    ) {
        return increment > UINT32_MAX - value
            ? UINT32_MAX
            : value + increment;
    }

    std::array<uint32_t, KIND_COUNT> values{};
};

static_assert(RealtimeWorkCounters::KIND_COUNT == 6U);
static_assert(sizeof(RealtimeWorkCounters) == 6U * sizeof(uint32_t));

}  // namespace test_support

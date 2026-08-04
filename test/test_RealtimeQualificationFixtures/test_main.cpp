#include <array>
#include <cassert>
#include <cstdint>
#include <iostream>
#include <type_traits>

#include <oc/time/Time.hpp>

#include "support/AdvancingMicrosClock.hpp"
#include "support/RealtimeWorkCounters.hpp"

namespace {

void test_clock_advances_per_read_and_wraps() {
    test_support::AdvancingMicrosClock clock;
    clock.install(1'000U);

    assert(oc::time::micros32() == 1'000U);
    assert(clock.readCount() == 1U);

    clock.advanceOnReadFrom(1'000U, 250U);
    assert(oc::time::micros32() == 1'250U);
    assert(oc::time::micros32() == 1'500U);
    assert(clock.currentUs() == 1'500U);
    assert(clock.incrementPerReadUs() == 250U);
    assert(clock.readCount() == 2U);

    clock.advanceOnReadFrom(UINT32_MAX - 9U, 6U);
    assert(oc::time::micros32() == UINT32_MAX - 3U);
    assert(oc::time::micros32() == 2U);
    assert(clock.readCount() == 2U);
}

void test_each_work_counter_saturates_exactly() {
    using test_support::RealtimeWorkCounters;
    using test_support::RealtimeWorkKind;

    static_assert(std::is_trivially_copyable_v<RealtimeWorkCounters>);
    constexpr std::array kinds{
        RealtimeWorkKind::QueueShift,
        RealtimeWorkKind::ResolverComparison,
        RealtimeWorkKind::EngineStep,
        RealtimeWorkKind::GraphVisit,
        RealtimeWorkKind::CcDeadlineGroup,
        RealtimeWorkKind::ClockCatchUp,
    };

    RealtimeWorkCounters counters{};
    for (const auto kind : kinds) {
        counters.reset();
        counters.add(kind, UINT32_MAX - 1U);
        assert(counters.get(kind) == UINT32_MAX - 1U);
        counters.add(kind);
        assert(counters.get(kind) == UINT32_MAX);
        counters.add(kind, UINT32_MAX);
        assert(counters.get(kind) == UINT32_MAX);
    }
}

}  // namespace

int main() {
    test_clock_advances_per_read_and_wraps();
    test_each_work_counter_saturates_exactly();
    std::cout << "Realtime qualification fixture tests passed\n";
    return 0;
}

#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cstdint>
#include <iostream>

#include "diagnostics/PsramSpanTracker.hpp"

namespace {

namespace detail = core::diagnostics::detail;

constexpr uint32_t POOL_BYTES = 131'072U;
constexpr uint32_t ALLOCATION_OVERHEAD = 16U;
constexpr uint32_t SPAN_STRIDE = 64U;
constexpr uint32_t SPAN_BYTES = 32U;
constexpr uint32_t USER_BYTES = 16U;

detail::PsramSpan spanAt(uint32_t index) {
    const uint32_t begin = index * SPAN_STRIDE;
    return {begin, begin + SPAN_BYTES, USER_BYTES};
}

void fillTo(
    detail::PsramSpanTracker& tracker,
    detail::PsramSpanTable& table,
    uint32_t count
) {
    for (uint32_t index = 0U; index < count; ++index) {
        assert(tracker.insert(table, spanAt(index)));
    }
}

void test_final_capacity_reuse_and_explicit_overflow() {
    detail::PsramSpanTable table{};
    detail::PsramSpanTracker tracker;
    tracker.reset(POOL_BYTES, ALLOCATION_OVERHEAD);

    fillTo(tracker, table, 723U);
    auto snapshot = tracker.snapshot();
    assert(snapshot.blockCount == 723U);
    assert(snapshot.largestBlockValid && !snapshot.overflow);

    for (uint32_t index = 723U;
         index < detail::PSRAM_SPAN_CAPACITY;
         ++index) {
        assert(tracker.insert(table, spanAt(index)));
    }
    snapshot = tracker.snapshot();
    assert(snapshot.blockCount == detail::PSRAM_SPAN_CAPACITY);
    assert(snapshot.allocatedBytes ==
        detail::PSRAM_SPAN_CAPACITY * SPAN_BYTES);
    assert(snapshot.userBytes ==
        detail::PSRAM_SPAN_CAPACITY * USER_BYTES);
    assert(snapshot.largestBlockValid && !snapshot.overflow);

    constexpr uint32_t REUSED_INDEX = 511U;
    const auto reused = spanAt(REUSED_INDEX);
    assert(tracker.remove(table, reused));
    assert(tracker.snapshot().blockCount ==
        detail::PSRAM_SPAN_CAPACITY - 1U);
    assert(tracker.insert(table, reused));
    assert(tracker.snapshot().blockCount == detail::PSRAM_SPAN_CAPACITY);

    const uint32_t lastExactLargest = tracker.snapshot().largestBlock;
    assert(!tracker.insert(table, spanAt(detail::PSRAM_SPAN_CAPACITY)));
    snapshot = tracker.snapshot();
    assert(snapshot.overflow);
    assert(!snapshot.largestBlockValid);
    assert(snapshot.blockCount == detail::PSRAM_SPAN_CAPACITY + 1U);
    assert(snapshot.allocatedBytes ==
        (detail::PSRAM_SPAN_CAPACITY + 1U) * SPAN_BYTES);
    assert(snapshot.userBytes ==
        (detail::PSRAM_SPAN_CAPACITY + 1U) * USER_BYTES);
    assert(snapshot.largestBlock == lastExactLargest);

    std::cout << "[PASS] 723/final capacity, reuse and overflow\n";
}

void test_history_milestones() {
    detail::PsramSpanTable table{};
    detail::PsramSpanTracker tracker;
    tracker.reset(POOL_BYTES, ALLOCATION_OVERHEAD);

    fillTo(tracker, table, 655U);
    assert(tracker.snapshot().blockCount == 655U);
    for (uint32_t index = 655U; index < 723U; ++index) {
        assert(tracker.insert(table, spanAt(index)));
    }
    const auto snapshot = tracker.snapshot();
    assert(snapshot.blockCount == 723U);
    assert(snapshot.largestBlockValid && !snapshot.overflow);

    std::cout << "[PASS] retained and prepared History milestones\n";
}

void test_true_zero_is_valid_and_distinct_from_overflow() {
    detail::PsramSpanTable table{};
    detail::PsramSpanTracker tracker;
    tracker.reset(1'024U, ALLOCATION_OVERHEAD);
    assert(tracker.insert(table, {0U, 1'024U, 1'000U}));

    const auto snapshot = tracker.snapshot();
    assert(snapshot.ready);
    assert(!snapshot.overflow);
    assert(snapshot.largestBlockValid);
    assert(snapshot.largestBlock == 0U);
    assert(snapshot.allocatedBytes == 1'024U);

    std::cout << "[PASS] valid zero largest block is unambiguous\n";
}

void test_duplicate_unknown_and_overlap_fail_closed() {
    detail::PsramSpanTable table{};
    detail::PsramSpanTracker tracker;

    tracker.reset(POOL_BYTES, ALLOCATION_OVERHEAD);
    assert(tracker.insert(table, spanAt(0U)));
    assert(!tracker.insert(table, spanAt(0U)));
    auto snapshot = tracker.snapshot();
    assert(snapshot.overflow && !snapshot.largestBlockValid);
    assert(snapshot.blockCount == 1U);

    tracker.reset(POOL_BYTES, ALLOCATION_OVERHEAD);
    assert(tracker.insert(table, spanAt(0U)));
    assert(!tracker.remove(table, spanAt(1U)));
    snapshot = tracker.snapshot();
    assert(snapshot.overflow && snapshot.blockCount == 1U);

    tracker.reset(POOL_BYTES, ALLOCATION_OVERHEAD);
    assert(tracker.insert(table, spanAt(0U)));
    assert(!tracker.insert(table, {16U, 48U, USER_BYTES}));
    snapshot = tracker.snapshot();
    assert(snapshot.overflow && snapshot.blockCount == 1U);

    std::cout << "[PASS] duplicate, unknown and overlap fail closed\n";
}

}  // namespace

int main() {
    static_assert(sizeof(detail::PsramSpan) == 12U);
    static_assert(detail::PSRAM_SPAN_CAPACITY == 1'034U);
    static_assert(detail::PSRAM_SPAN_TABLE_BYTES == 12'408U);

    test_history_milestones();
    test_final_capacity_reuse_and_explicit_overflow();
    test_true_zero_is_valid_and_distinct_from_overflow();
    test_duplicate_unknown_and_overlap_fail_closed();
    std::cout << "PsramSpanTracker tests passed\n";
    return 0;
}

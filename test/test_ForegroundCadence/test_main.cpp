#include <cassert>
#include <cstdint>

#include <app/PhaseRetainingDeadline.hpp>

namespace {

using FastDeadline = core::app::PhaseRetainingDeadline<100U>;
using SlowDeadline = core::app::PhaseRetainingDeadline<400U>;

static_assert(sizeof(FastDeadline) == sizeof(uint32_t));

void test_before_exact_after_and_one_consume_per_pass() {
    FastDeadline exact;
    assert(!exact.consumeIfDue(0U));
    assert(!exact.consumeIfDue(99U));
    assert(exact.consumeIfDue(100U));
    assert(!exact.consumeIfDue(100U));

    FastDeadline after;
    assert(after.consumeIfDue(101U));
    assert(!after.consumeIfDue(199U));
    assert(after.consumeIfDue(200U));
}

void test_ordinary_overshoot_retains_phase() {
    FastDeadline deadline;
    assert(deadline.consumeIfDue(125U));
    assert(!deadline.consumeIfDue(199U));
    assert(deadline.consumeIfDue(200U));
}

void test_variable_work_never_resets_phase() {
    FastDeadline deadline;
    assert(deadline.consumeIfDue(100U));
    assert(deadline.consumeIfDue(205U));
    assert(deadline.consumeIfDue(317U));
    assert(deadline.consumeIfDue(421U));
    assert(!deadline.consumeIfDue(499U));
    assert(deadline.consumeIfDue(500U));
}

void test_twenty_millisecond_stall_skips_without_replay() {
    FastDeadline deadline;
    assert(deadline.consumeIfDue(20'000U));
    assert(!deadline.consumeIfDue(20'000U));
    assert(!deadline.consumeIfDue(20'099U));
    assert(deadline.consumeIfDue(20'100U));
}

void test_deadline_is_wrap_safe() {
    FastDeadline deadline(UINT32_MAX - 50U);
    assert(!deadline.consumeIfDue(48U));
    assert(deadline.consumeIfDue(49U));
    assert(!deadline.consumeIfDue(148U));
    assert(deadline.consumeIfDue(149U));
}

void test_app_and_lvgl_classes_advance_independently() {
    FastDeadline app;
    SlowDeadline lvgl;
    constexpr uint32_t passes[] = {99U, 100U, 199U, 200U, 299U, 300U, 399U, 400U};
    uint32_t appCount = 0U;
    uint32_t lvglCount = 0U;

    for (const uint32_t nowUs : passes) {
        if (app.consumeIfDue(nowUs)) ++appCount;
        if (lvgl.consumeIfDue(nowUs)) ++lvglCount;
    }

    assert(appCount == 4U);
    assert(lvglCount == 1U);
}

}  // namespace

int main() {
    test_before_exact_after_and_one_consume_per_pass();
    test_ordinary_overshoot_retains_phase();
    test_variable_work_never_resets_phase();
    test_twenty_millisecond_stall_skips_without_replay();
    test_deadline_is_wrap_safe();
    test_app_and_lvgl_classes_advance_independently();
    return 0;
}

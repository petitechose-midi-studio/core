#include <cassert>
#include <cstdint>
#include <iostream>
#include <vector>

#include "../../src/state/shared/StructureSlotOps.hpp"

namespace {

namespace slots = core::state::shared;

void test_remove_index_keeps_at_least_one_enabled_slot() {
    const auto unchanged = slots::removeIndex(0x0001, 0, 4);
    assert(!unchanged.changed);
    assert(unchanged.nextMask == 0x0001);
    assert(unchanged.nextActive == 0);

    const auto removed = slots::removeIndex(0x000B, 1, 4);
    assert(removed.changed);
    assert(removed.nextMask == 0x0009);
    assert(removed.nextActive == 3);

    std::cout << "[PASS] test_remove_index_keeps_at_least_one_enabled_slot\n";
}

void test_enabled_navigation_wraps_over_gaps() {
    assert(slots::nextEnabledIndex(0x0009, 0, 4, 1) == 3);
    assert(slots::nextEnabledIndex(0x0009, 0, 4, -1) == 3);
    assert(slots::nextEnabledIndex(0x0009, 3, 4, 1) == 0);
    assert(slots::nextEnabledIndex(0x0008, 3, 4, 1) == 3);

    std::cout << "[PASS] test_enabled_navigation_wraps_over_gaps\n";
}

void test_add_slot_navigation_is_terminal_after_highest_enabled_slot() {
    const auto add = slots::nextNavigationTarget(0x0003, 1, 4, false, 1);
    assert(add.valid);
    assert(add.addSlot);
    assert(add.index == 2);

    const auto hold = slots::nextNavigationTarget(0x0003, 2, 4, true, 1);
    assert(hold.valid);
    assert(hold.addSlot);
    assert(hold.index == 2);

    const auto back = slots::nextNavigationTarget(0x0003, 2, 4, true, -1);
    assert(back.valid);
    assert(!back.addSlot);
    assert(back.index == 1);

    const auto fullWrap = slots::nextNavigationTarget(0x000F, 3, 4, false, 1);
    assert(fullWrap.valid);
    assert(!fullWrap.addSlot);
    assert(fullWrap.index == 0);

    std::cout << "[PASS] test_add_slot_navigation_is_terminal_after_highest_enabled_slot\n";
}

}  // namespace

int main() {
    test_remove_index_keeps_at_least_one_enabled_slot();
    test_enabled_navigation_wraps_over_gaps();
    test_add_slot_navigation_is_terminal_after_highest_enabled_slot();

    std::cout << "All StructureSlotOps tests passed\n";
    return 0;
}

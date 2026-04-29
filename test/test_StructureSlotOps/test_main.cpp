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

void test_remove_selected_rejects_empty_or_full_delete() {
    const auto empty = slots::removeSelected(0x0007, 0x0000, 1, 4);
    assert(!empty.changed);
    assert(empty.nextMask == 0x0007);
    assert(empty.nextActive == 1);

    const auto all = slots::removeSelected(0x0007, 0x0007, 1, 4);
    assert(!all.changed);
    assert(all.nextMask == 0x0007);
    assert(all.nextActive == 1);

    const auto partial = slots::removeSelected(0x0007, 0x0002, 1, 4);
    assert(partial.changed);
    assert(partial.nextMask == 0x0005);
    assert(partial.nextActive == 2);

    std::cout << "[PASS] test_remove_selected_rejects_empty_or_full_delete\n";
}

void test_duplicate_selection_copies_enabled_selected_slots_into_free_slots() {
    std::vector<uint8_t> copiedSources;
    std::vector<uint8_t> copiedDestinations;

    const auto result = slots::duplicateSelectionIntoFreeSlots(
        0x000B,
        0x0003,
        5,
        [&](uint8_t source, uint8_t dest) {
            copiedSources.push_back(source);
            copiedDestinations.push_back(dest);
        }
    );

    assert(result.changed);
    assert(result.nextMask == 0x001F);
    assert(result.firstDuplicated == 2);
    assert(copiedSources.size() == 2);
    assert(copiedSources[0] == 0);
    assert(copiedDestinations[0] == 2);
    assert(copiedSources[1] == 1);
    assert(copiedDestinations[1] == 4);

    std::cout << "[PASS] test_duplicate_selection_copies_enabled_selected_slots_into_free_slots\n";
}

void test_duplicate_selection_stops_when_no_free_slot_remains() {
    uint8_t copyCount = 0;
    const auto result = slots::duplicateSelectionIntoFreeSlots(
        0x0007,
        0x0007,
        4,
        [&](uint8_t, uint8_t dest) {
            ++copyCount;
            assert(dest == 3);
        }
    );

    assert(result.changed);
    assert(result.nextMask == 0x000F);
    assert(result.firstDuplicated == 3);
    assert(copyCount == 1);

    std::cout << "[PASS] test_duplicate_selection_stops_when_no_free_slot_remains\n";
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
    test_remove_selected_rejects_empty_or_full_delete();
    test_duplicate_selection_copies_enabled_selected_slots_into_free_slots();
    test_duplicate_selection_stops_when_no_free_slot_remains();
    test_enabled_navigation_wraps_over_gaps();
    test_add_slot_navigation_is_terminal_after_highest_enabled_slot();

    std::cout << "All StructureSlotOps tests passed\n";
    return 0;
}

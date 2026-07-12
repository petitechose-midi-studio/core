#ifdef NDEBUG
#undef NDEBUG
#endif

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <iostream>

#include "../../src/handler/macro/MacroStructureAutomationOps.hpp"

namespace {

namespace macro = core::state::macro;
namespace ops = core::handler::macro_structure_automation_ops;

macro::MacroAutomationLane makeLane(uint16_t pointCount) {
    macro::MacroAutomationLane lane;
    lane.active = true;
    lane.durationBeats = 300.0f;
    for (uint16_t i = 0; i < pointCount; ++i) {
        assert(macro::macroAutomationAppendPoint(
            lane,
            static_cast<float>(i) * 0.125f,
            (i & 1U) == 0U ? 0.25f : 0.75f
        ));
    }
    return lane;
}

void assignLane(macro::MacroAutomationBankState& bank,
                macro::MacroAutomationSlotAddress address,
                uint16_t pointCount) {
    auto* slot = macro::macroAutomationGetOrCreateSlot(bank, address);
    assert(slot != nullptr);
    auto lane = makeLane(pointCount);
    assert(macro::macroAutomationAssignAutomation(bank, *slot, lane));
}

void fillRemainingPoolOutsideTrackZero(macro::MacroAutomationBankState& bank) {
    uint8_t page = 0;
    uint8_t macroIndex = 0;
    while (bank.pointPool.used < macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY) {
        const uint16_t remaining = static_cast<uint16_t>(
            macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY - bank.pointPool.used
        );
        const uint16_t pointCount = std::min<uint16_t>(
            remaining,
            macro::MACRO_AUTOMATION_RECORDING_MAX_POINTS
        );
        assignLane(
            bank,
            macro::MacroAutomationSlotAddress{
                .track = 1,
                .page = page,
                .macro = macroIndex,
            },
            pointCount
        );
        macroIndex = static_cast<uint8_t>(macroIndex + 1U);
        if (macroIndex >= macro::MACRO_COUNT) {
            macroIndex = 0;
            page = static_cast<uint8_t>(page + 1U);
        }
    }
}

void test_track_duplication_preserves_automation_from_every_page() {
    macro::MacroAutomationBankState bank;
    assignLane(bank, {.track = 0, .page = 0, .macro = 1}, 2);
    assignLane(bank, {.track = 0, .page = 7, .macro = 5}, 3);

    assert(ops::duplicateTrack(bank, 0, 3));

    const auto* first = macro::macroAutomationFindSlot(
        bank,
        {.track = 3, .page = 0, .macro = 1}
    );
    const auto* second = macro::macroAutomationFindSlot(
        bank,
        {.track = 3, .page = 7, .macro = 5}
    );
    assert(first != nullptr && first->automation.active);
    assert(first->automation.pointCount == 2);
    assert(second != nullptr && second->automation.active);
    assert(second->automation.pointCount == 3);

    std::cout << "[PASS] test_track_duplication_preserves_automation_from_every_page\n";
}

void test_page_duplication_rejects_full_pool_without_partial_state() {
    macro::MacroAutomationBankState bank;
    const auto sourceAddress = macro::MacroAutomationSlotAddress{
        .track = 0,
        .page = 0,
        .macro = 0,
    };
    const auto destAddress = macro::MacroAutomationSlotAddress{
        .track = 0,
        .page = 1,
        .macro = 0,
    };
    assignLane(bank, sourceAddress, 2);
    fillRemainingPoolOutsideTrackZero(bank);
    const uint8_t entryCountBefore = bank.entryCount;

    assert(!ops::duplicatePage(bank, 0, 0, 0, 1));

    assert(bank.entryCount == entryCountBefore);
    assert(bank.pointPool.used == macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY);
    assert(macro::macroAutomationFindSlot(bank, sourceAddress) != nullptr);
    assert(macro::macroAutomationFindSlot(bank, destAddress) == nullptr);

    std::cout << "[PASS] test_page_duplication_rejects_full_pool_without_partial_state\n";
}

void test_empty_page_clipboard_replaces_existing_automation_with_empty_scope() {
    macro::MacroAutomationBankState bank;
    const auto destAddress = macro::MacroAutomationSlotAddress{
        .track = 2,
        .page = 4,
        .macro = 6,
    };
    assignLane(bank, destAddress, 2);

    core::state::MacroAutomationClipboard clipboard;
    clipboard.trackScope = false;

    assert(ops::replacePageFromClipboard(bank, 2, 4, &clipboard));
    assert(macro::macroAutomationFindSlot(bank, destAddress) == nullptr);
    assert(bank.pointPool.used == 0);

    std::cout << "[PASS] test_empty_page_clipboard_replaces_existing_automation_with_empty_scope\n";
}

void test_empty_structure_copy_does_not_allocate_automation_clipboard() {
    macro::MacroAutomationBankState sourceBank;
    core::state::StructureClipboardState clipboard;
    core::state::macro::MacroPageData page;

    assert(clipboard.storeMacroPage(page, sourceBank, 0, 0));
    assert(clipboard.hasMacroPage());
    assert(clipboard.macroAutomationSet == nullptr);

    macro::MacroAutomationBankState destBank;
    const auto destAddress = macro::MacroAutomationSlotAddress{
        .track = 3,
        .page = 2,
        .macro = 4,
    };
    assignLane(destBank, destAddress, 2);
    assert(ops::replacePageFromClipboard(
        destBank,
        destAddress.track,
        destAddress.page,
        clipboard.macroAutomationSet.get()
    ));
    assert(macro::macroAutomationFindSlot(destBank, destAddress) == nullptr);

    std::cout << "[PASS] test_empty_structure_copy_does_not_allocate_automation_clipboard\n";
}

void test_track_structure_copy_captures_all_page_automations() {
    macro::MacroAutomationBankState sourceBank;
    assignLane(sourceBank, {.track = 2, .page = 1, .macro = 3}, 2);
    assignLane(sourceBank, {.track = 2, .page = 9, .macro = 6}, 3);
    core::state::StructureClipboardState clipboard;
    core::state::macro::MacroTrackData track;

    assert(clipboard.storeMacroTrack(track, sourceBank, 2));
    assert(clipboard.hasMacroTrack());
    assert(clipboard.macroAutomationSet != nullptr);
    assert(clipboard.macroAutomationSet->trackScope);
    assert(clipboard.macroAutomationSet->count == 2);

    std::cout << "[PASS] test_track_structure_copy_captures_all_page_automations\n";
}

}  // namespace

int main() {
    test_track_duplication_preserves_automation_from_every_page();
    test_page_duplication_rejects_full_pool_without_partial_state();
    test_empty_page_clipboard_replaces_existing_automation_with_empty_scope();
    test_empty_structure_copy_does_not_allocate_automation_clipboard();
    test_track_structure_copy_captures_all_page_automations();

    std::cout << "\nAll MacroStructureAutomationOps tests passed.\n";
    return 0;
}

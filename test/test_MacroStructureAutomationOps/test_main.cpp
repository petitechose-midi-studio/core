#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "handler/macro/MacroStructureAutomationOps.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "../support/ProjectControlTestUtils.hpp"

namespace {

namespace macro = core::state::macro;
namespace modulation = core::state::modulation;
namespace ops = core::handler::macro_structure_automation_ops;

macro::MacroAutomationLane makeLane(uint16_t pointCount) {
    macro::MacroAutomationLane lane;
    lane.active = true;
    lane.durationBeats = 300.0f;
    for (uint16_t index = 0; index < pointCount; ++index) {
        assert(macro::macroAutomationAppendPoint(
            lane,
            static_cast<float>(index) * 0.125f,
            (index & 1U) == 0U ? 0.25f : 0.75f
        ));
    }
    return lane;
}

void assignLane(
    modulation::ProjectControlState& control,
    macro::MacroAutomationSlotAddress address,
    uint16_t pointCount
) {
    const auto lane = makeLane(pointCount);
    assert(test_support::project_control::assignAutomation(
        control,
        address,
        lane
    ));
}

void test_track_duplication_preserves_automation_from_every_page() {
    modulation::ProjectControlState control;
    assignLane(control, {.track = 0, .page = 0, .macro = 1}, 2);
    assignLane(control, {.track = 0, .page = 7, .macro = 5}, 3);

    const ops::ProjectControlTrackCopy copy{
        .sourceTrack = 0,
        .destTrack = 3,
    };
    assert(ops::duplicateTracks(control, &copy, 1));

    const auto first = test_support::project_control::readSlot(
        control,
        {.track = 3, .page = 0, .macro = 1}
    );
    const auto second = test_support::project_control::readSlot(
        control,
        {.track = 3, .page = 7, .macro = 5}
    );
    assert(first.automationStored && first.legacy.automation.pointCount == 2);
    assert(second.automationStored && second.legacy.automation.pointCount == 3);

    std::cout
        << "[PASS] Project track duplication preserves every page automation\n";
}

void test_batch_duplication_rejects_invalid_copy_atomically() {
    modulation::ProjectControlState control;
    assignLane(control, {.track = 0, .page = 0, .macro = 0}, 2);
    const auto before = control;
    const std::array<ops::ProjectControlPageCopy, 2> copies{{
        {
            .sourceTrack = 0,
            .sourcePage = 0,
            .destTrack = 0,
            .destPage = 1,
        },
        {
            .sourceTrack = 0,
            .sourcePage = 0,
            .destTrack = 0,
            .destPage = macro::PAGE_COUNT,
        },
    }};

    assert(!ops::duplicatePages(
        control,
        copies.data(),
        static_cast<uint8_t>(copies.size())
    ));
    assert(std::memcmp(&control, &before, sizeof(control)) == 0);

    std::cout << "[PASS] Project batch duplication failure is atomic\n";
}

void test_empty_page_clipboard_replaces_existing_control_with_empty_scope() {
    modulation::ProjectControlState control;
    const macro::MacroAutomationSlotAddress destAddress{
        .track = 2,
        .page = 4,
        .macro = 6,
    };
    assignLane(control, destAddress, 2);

    core::state::MacroAutomationClipboard clipboard;
    clipboard.trackScope = false;
    assert(ops::replacePageFromClipboard(
        control,
        destAddress.track,
        destAddress.page,
        &clipboard
    ));
    const auto result = test_support::project_control::readSlot(
        control,
        destAddress
    );
    assert(!result.present);
    assert(control.authored.curves.recordCount == 0);
    assert(control.authored.curves.pointCount == 0);

    std::cout << "[PASS] Empty page clipboard clears the Project scope\n";
}

void test_empty_structure_copy_does_not_allocate_control_clipboard() {
    modulation::ProjectControlState sourceControl;
    core::state::StructureClipboardState clipboard;
    macro::MacroPageData page;

    assert(clipboard.storeMacroPage(page, sourceControl, 0, 0));
    assert(clipboard.hasMacroPage());
    assert(clipboard.macroAutomationSet == nullptr);

    modulation::ProjectControlState destControl;
    const macro::MacroAutomationSlotAddress destAddress{
        .track = 3,
        .page = 2,
        .macro = 4,
    };
    assignLane(destControl, destAddress, 2);
    assert(ops::replacePageFromClipboard(
        destControl,
        destAddress.track,
        destAddress.page,
        clipboard.macroAutomationSet.get()
    ));
    assert(!test_support::project_control::readSlot(
        destControl,
        destAddress
    ).present);

    std::cout << "[PASS] Empty structure copy allocates no control payload\n";
}

void test_track_structure_copy_captures_all_page_automation() {
    modulation::ProjectControlState sourceControl;
    assignLane(sourceControl, {.track = 2, .page = 1, .macro = 3}, 2);
    assignLane(sourceControl, {.track = 2, .page = 9, .macro = 6}, 3);
    core::state::StructureClipboardState clipboard;
    macro::MacroTrackData track;

    assert(clipboard.storeMacroTrack(track, sourceControl, 2));
    assert(clipboard.hasMacroTrack());
    assert(clipboard.macroAutomationSet != nullptr);
    assert(clipboard.macroAutomationSet->trackScope);
    assert(clipboard.macroAutomationSet->count == 2);

    std::cout << "[PASS] Track copy captures every Project automation\n";
}

void test_malformed_clipboard_is_rejected_before_destination_mutation() {
    modulation::ProjectControlState control;
    const macro::MacroAutomationSlotAddress destAddress{
        .track = 4,
        .page = 3,
        .macro = 2,
    };
    assignLane(control, destAddress, 3);
    const auto before = control;

    core::state::MacroAutomationClipboard clipboard;
    clipboard.valid = true;
    clipboard.trackScope = false;
    clipboard.count = 1;
    auto& entry = clipboard.entries[0];
    entry.valid = true;
    entry.sourceMacro = 1;
    entry.state.automation.active = true;
    entry.state.automation.pointOffset =
        macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY;
    entry.state.automation.pointCount = 1;

    assert(!ops::replacePageFromClipboard(
        control,
        destAddress.track,
        destAddress.page,
        &clipboard
    ));
    assert(std::memcmp(&control, &before, sizeof(control)) == 0);

    clipboard.count = static_cast<uint8_t>(clipboard.entries.size() + 1U);
    assert(!ops::replacePageFromClipboard(
        control,
        destAddress.track,
        destAddress.page,
        &clipboard
    ));
    assert(std::memcmp(&control, &before, sizeof(control)) == 0);

    clipboard.count = 0;
    clipboard.pointPool.used = static_cast<uint16_t>(
        clipboard.pointPool.points.size() + 1U
    );
    assert(!ops::replacePageFromClipboard(
        control,
        destAddress.track,
        destAddress.page,
        &clipboard
    ));
    assert(std::memcmp(&control, &before, sizeof(control)) == 0);

    std::cout << "[PASS] Malformed clipboard cannot mutate Project control\n";
}

}  // namespace

int main() {
    test_track_duplication_preserves_automation_from_every_page();
    test_batch_duplication_rejects_invalid_copy_atomically();
    test_empty_page_clipboard_replaces_existing_control_with_empty_scope();
    test_empty_structure_copy_does_not_allocate_control_clipboard();
    test_track_structure_copy_captures_all_page_automation();
    test_malformed_clipboard_is_rejected_before_destination_mutation();

    std::cout << "\nAll MacroStructureAutomationOps tests passed.\n";
    return 0;
}

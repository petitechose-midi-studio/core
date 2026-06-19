#include <cassert>
#include <iostream>

#include "state/StructureClipboardPastePlan.hpp"

namespace {

void test_page_selection_paste_plan_projects_offsets_and_overwrite() {
    core::state::SequencerPageSelectionClipboard clipboard;
    clipboard.valid = true;
    clipboard.sourceFirstPage = 1;
    clipboard.count = 2;
    clipboard.pages[0].valid = true;
    clipboard.pages[0].sourcePage = 1;
    clipboard.pages[1].valid = true;
    clipboard.pages[1].sourcePage = 3;

    const auto plan = core::state::buildSequencerPageSelectionPastePlan(
        clipboard,
        4,
        6
    );

    assert(plan.hasEntries());
    assert(plan.count == 2);
    assert(plan.firstDestinationPage == 4);
    assert(plan.entries[0].clipboardIndex == 0);
    assert(plan.entries[0].destinationPage == 4);
    assert(plan.entries[1].clipboardIndex == 1);
    assert(plan.entries[1].destinationPage == 6);
    assert(plan.destinationMask == ((1U << 4) | (1U << 6)));
    assert(plan.overwriteMask == (1U << 4));

    std::cout << "[PASS] test_page_selection_paste_plan_projects_offsets_and_overwrite\n";
}

void test_page_selection_paste_plan_clips_after_page_limit() {
    core::state::SequencerPageSelectionClipboard clipboard;
    clipboard.valid = true;
    clipboard.sourceFirstPage = 0;
    clipboard.count = 2;
    clipboard.pages[0].valid = true;
    clipboard.pages[0].sourcePage = 0;
    clipboard.pages[1].valid = true;
    clipboard.pages[1].sourcePage = 2;

    const auto plan = core::state::buildSequencerPageSelectionPastePlan(
        clipboard,
        15,
        16
    );

    assert(plan.hasEntries());
    assert(plan.count == 1);
    assert(plan.firstDestinationPage == 15);
    assert(plan.entries[0].clipboardIndex == 0);
    assert(plan.entries[0].destinationPage == 15);
    assert(plan.destinationMask == (1U << 15));
    assert(plan.overwriteMask == (1U << 15));

    std::cout << "[PASS] test_page_selection_paste_plan_clips_after_page_limit\n";
}

}  // namespace

int main() {
    test_page_selection_paste_plan_projects_offsets_and_overwrite();
    test_page_selection_paste_plan_clips_after_page_limit();
    return 0;
}

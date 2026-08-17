#include <cassert>
#include <iostream>

#include "state/macro/MacroSlotClipboardPlan.hpp"
#include "../support/ProjectControlTestUtils.hpp"

namespace {

using core::state::ClipboardTransferAvailability;
using core::state::ClipboardTransferReason;
namespace macro = core::state::macro;

void storeSparseClipboard(
    macro::MacroPagesState& pages,
    core::state::StructureClipboardState& clipboard
) {
    pages.tracks[0].enabledPageMask = 0x0003U;
    pages.tracks[0].pages[0].setMacroActive(1U, true);
    pages.tracks[0].pages[0].setMacroActive(4U, true);
    pages.tracks[0].pages[0].cc[1] = 21U;
    pages.tracks[0].pages[0].cc[4] = 24U;
    pages.syncActiveTrackCache();

    oc::note::sequencer::StepBitMask128 selected{};
    selected.setBit(1U, true);
    selected.setBit(4U, true);
    assert(clipboard.storeMacroSlotSelection(pages, 0U, selected));
}

void test_sparse_offsets_and_collision_warning_are_exact() {
    macro::MacroPagesState pages;
    core::state::StructureClipboardState clipboard;
    storeSparseClipboard(pages, clipboard);
    pages.tracks[0].pages[1].setMacroActive(5U, true);

    const auto plan = macro::buildMacroSlotClipboardPlan(
        clipboard,
        pages,
        0U,
        10U
    );

    assert(plan.canCommit());
    assert(plan.availability == ClipboardTransferAvailability::WARNING);
    assert(plan.reason == ClipboardTransferReason::NONE);
    assert(plan.sourceCount == 2U);
    assert(plan.count == 2U);
    assert(plan.firstSourceLinear == 1U);
    assert(plan.lastSourceLinear == 4U);
    assert(plan.entries[0].targetLinear == 10U);
    assert(plan.entries[1].targetLinear == 13U);
    assert(plan.entries[0].targetCc == 10U);
    assert(plan.entries[1].targetCc == 13U);
    assert(plan.destinationMasks[1] ==
           static_cast<uint8_t>((1U << 2U) | (1U << 5U)));
    assert(plan.overwriteMasks[1] ==
           static_cast<uint8_t>(1U << 5U));
    assert(plan.overwriteCount == 1U);
    assert(plan.createPageMask == 0U);

    std::cout
        << "[PASS] sparse Macro footprint and overwrite warning are exact\n";
}

void test_one_virtual_page_materializes_without_intermediate_pages() {
    macro::MacroPagesState pages;
    core::state::StructureClipboardState clipboard;
    storeSparseClipboard(pages, clipboard);

    const auto plan = macro::buildMacroSlotClipboardPlan(
        clipboard,
        pages,
        0U,
        18U
    );

    assert(plan.canCommit());
    assert(plan.availability == ClipboardTransferAvailability::READY);
    assert(plan.existingPageCount == 2U);
    assert(plan.allowedPageCount == 3U);
    assert(plan.requiredPageCount == 3U);
    assert(plan.createPageMask == 0x0004U);
    assert(plan.entries[0].targetPage == 2U);
    assert(plan.entries[0].targetMacro == 2U);
    assert(plan.entries[1].targetPage == 2U);
    assert(plan.entries[1].targetMacro == 5U);
    assert(plan.entries[0].targetCc == 19U);
    assert(plan.entries[1].targetCc == 22U);
    assert(plan.overwriteCount == 0U);

    std::cout
        << "[PASS] exactly one virtual Macro Page is materializable\n";
}

void test_second_nonexistent_page_blocks_without_clipping() {
    macro::MacroPagesState pages;
    core::state::StructureClipboardState clipboard;
    storeSparseClipboard(pages, clipboard);

    const auto plan = macro::buildMacroSlotClipboardPlan(
        clipboard,
        pages,
        0U,
        22U
    );

    assert(!plan.canCommit());
    assert(plan.availability == ClipboardTransferAvailability::DISABLED);
    assert(plan.reason == ClipboardTransferReason::OUT_OF_RANGE);
    assert(plan.sourceCount == 2U);
    assert(plan.count == 1U);
    assert(plan.entries[0].targetLinear == 22U);

    std::cout
        << "[PASS] a second nonexistent Macro Page blocks without clipping\n";
}

void test_full_automation_domain_blocks_the_whole_plan() {
    namespace modulation = core::state::modulation;

    macro::MacroPagesState pages;
    core::state::StructureClipboardState clipboard;
    pages.tracks[0].enabledPageMask = 0x0003U;
    pages.tracks[0].pages[0].setMacroActive(1U, true);
    pages.syncActiveTrackCache();

    core::state::macro::MacroAutomationLane lane;
    lane.durationBeats = 1.0f;
    assert(core::state::macro::macroAutomationAppendPoint(
        lane,
        0.0f,
        0.25f
    ));
    assert(core::state::macro::macroAutomationAppendPoint(
        lane,
        1.0f,
        0.75f
    ));

    const macro::MacroAutomationSlotAddress source{
        .track = 0U,
        .page = 0U,
        .macro = 1U,
    };
    const macro::MacroAutomationSlotAddress target{
        .track = 0U,
        .page = 1U,
        .macro = 0U,
    };
    assert(test_support::project_control::assignAutomation(
        pages.control,
        source,
        lane
    ));

    oc::note::sequencer::StepBitMask128 selected{};
    selected.setBit(1U, true);
    assert(clipboard.storeMacroSlotSelection(pages, 0U, selected));

    for (uint8_t track = 0U;
         track < macro::TRACK_COUNT &&
         pages.control.authored.automation.entryCount <
             modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY;
         ++track) {
        for (uint8_t page = 0U;
             page < macro::PAGE_COUNT &&
             pages.control.authored.automation.entryCount <
                 modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY;
             ++page) {
            for (uint8_t slot = 0U;
                 slot < macro::MACRO_COUNT &&
                 pages.control.authored.automation.entryCount <
                     modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY;
                 ++slot) {
                const macro::MacroAutomationSlotAddress address{
                    .track = track,
                    .page = page,
                    .macro = slot,
                };
                if (macro::macroAutomationAddressEquals(address, source) ||
                    macro::macroAutomationAddressEquals(address, target)) {
                    continue;
                }
                assert(test_support::project_control::assignAutomation(
                    pages.control,
                    address,
                    lane
                ));
            }
        }
    }
    assert(
        pages.control.authored.automation.entryCount ==
        modulation::PROJECT_AUTOMATION_ENTRY_CAPACITY
    );

    const auto plan = macro::buildMacroSlotClipboardPlan(
        clipboard,
        pages,
        0U,
        macro::MACRO_COUNT
    );

    assert(!plan.canCommit());
    assert(plan.availability == ClipboardTransferAvailability::DISABLED);
    assert(plan.reason == ClipboardTransferReason::CAPACITY);
    assert(plan.sourceCount == 1U);
    assert(plan.count == 1U);

    std::cout
        << "[PASS] full Automation capacity blocks the complete Macro paste\n";
}

void test_cc_search_wraps_once_and_preserves_the_sparse_footprint() {
    macro::MacroPagesState pages;
    core::state::StructureClipboardState clipboard;
    pages.tracks[0].pages[0].setMacroActive(1U, true);
    pages.tracks[0].pages[0].cc[1] = 21U;
    pages.syncActiveTrackCache();
    oc::note::sequencer::StepBitMask128 selected{};
    selected.setBit(1U, true);
    assert(clipboard.storeMacroSlotSelection(pages, 0U, selected));

    auto& target = pages.tracks[1];
    target.enabledPageMask = 0xFFFFU;
    for (auto& page : target.pages) page.activeMacroMask = 0U;
    target.pages[0].setMacroActive(0U, true);
    target.pages[0].cc[0] = 127U;

    const auto plan = macro::buildMacroSlotClipboardPlan(
        clipboard,
        pages,
        1U,
        127U
    );

    assert(plan.canCommit());
    assert(plan.entries[0].targetCc == 0U);

    std::cout << "[PASS] CC placement wraps at most once across 0..127\n";
}

void test_cc_capacity_blocks_the_whole_sparse_plan() {
    macro::MacroPagesState pages;
    core::state::StructureClipboardState clipboard;
    storeSparseClipboard(pages, clipboard);

    auto& target = pages.tracks[1];
    target.enabledPageMask = 0xFFFFU;
    for (uint8_t page = 0U; page < macro::PAGE_COUNT; ++page) {
        target.pages[page].activeMacroMask = 0xFFU;
        for (uint8_t slot = 0U; slot < macro::MACRO_COUNT; ++slot) {
            target.pages[page].cc[slot] = static_cast<uint8_t>(
                page * macro::MACRO_COUNT + slot
            );
        }
    }
    std::swap(target.pages[0].cc[0], target.pages[1].cc[2]);
    std::swap(target.pages[0].cc[3], target.pages[2].cc[4]);

    const auto plan = macro::buildMacroSlotClipboardPlan(
        clipboard,
        pages,
        1U,
        0U
    );

    assert(!plan.canCommit());
    assert(plan.reason == ClipboardTransferReason::CAPACITY);
    assert(plan.count == plan.sourceCount);

    std::cout << "[PASS] no complete CC footprint blocks the whole Paste\n";
}

void test_plan_identity_includes_the_resolved_ccs() {
    macro::MacroPagesState pages;
    core::state::StructureClipboardState clipboard;
    storeSparseClipboard(pages, clipboard);
    const auto plan = macro::buildMacroSlotClipboardPlan(
        clipboard,
        pages,
        0U,
        10U
    );
    auto changed = plan;
    changed.entries[0].targetCc = static_cast<uint8_t>(
        changed.entries[0].targetCc + 1U
    );

    assert(macro::sameMacroSlotClipboardPlan(plan, plan));
    assert(!macro::sameMacroSlotClipboardPlan(plan, changed));

    std::cout << "[PASS] resolved CCs participate in stale-plan detection\n";
}

}  // namespace

int main() {
    test_sparse_offsets_and_collision_warning_are_exact();
    test_one_virtual_page_materializes_without_intermediate_pages();
    test_second_nonexistent_page_blocks_without_clipping();
    test_full_automation_domain_blocks_the_whole_plan();
    test_cc_search_wraps_once_and_preserves_the_sparse_footprint();
    test_cc_capacity_blocks_the_whole_sparse_plan();
    test_plan_identity_includes_the_resolved_ccs();
    return 0;
}

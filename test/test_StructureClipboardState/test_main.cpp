#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <iostream>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace {

namespace macro = core::state::macro;
namespace sequencer = core::state::sequencer;

void test_cross_domain_copy_releases_inactive_owned_payloads() {
    core::state::StructureClipboardState clipboard;
    sequencer::SequencerState sequencerState;
    assert(sequencer::ensureGraphRoot(sequencerState.pattern));

    core::state::SequencerPageClipboard page;
    page.valid = true;
    page.count = 1;
    page.sourcePage = 0;
    assert(clipboard.storeSequencerPage(
        page,
        sequencer::graphView(sequencerState.pattern)
    ));
    assert(clipboard.sequencerGraph != nullptr);

    macro::MacroPageData macroPage;
    macro::MacroAutomationBankState automation;
    assert(clipboard.storeMacroPage(macroPage, automation, 0, 0));
    assert(clipboard.hasMacroPage());
    assert(clipboard.sequencerGraph == nullptr);
    assert(clipboard.sequencerTrackSelection == nullptr);

    std::cout << "[PASS] test_cross_domain_copy_releases_inactive_owned_payloads\n";
}

void test_rejected_copy_clears_previous_clipboard() {
    core::state::StructureClipboardState clipboard;
    macro::MacroAutomationBankState automation;
    const macro::MacroAutomationSlotAddress address{};
    auto* slot = macro::macroAutomationGetOrCreateSlot(automation, address);
    assert(slot != nullptr);

    macro::MacroAutomationLane lane;
    lane.active = true;
    lane.durationBeats = 1.0f;
    assert(macro::macroAutomationAppendPoint(lane, 0.0f, 0.25f));
    assert(macro::macroAutomationAppendPoint(lane, 1.0f, 0.75f));
    assert(macro::macroAutomationAssignAutomation(automation, *slot, lane));
    assert(clipboard.storeMacroAutomation(automation, *slot));
    assert(clipboard.hasMacroAutomation());

    core::state::SequencerStepsClipboard emptySteps;
    assert(!clipboard.storeSequencerSteps(emptySteps, nullptr));
    assert(clipboard.kind.get() == core::state::StructureClipboardKind::NONE);
    assert(clipboard.macroAutomationSet == nullptr);
    assert(clipboard.sequencerGraph == nullptr);
    assert(clipboard.sequencerTrackSelection == nullptr);

    std::cout << "[PASS] test_rejected_copy_clears_previous_clipboard\n";
}

void test_invalid_macro_automation_copy_reports_failure() {
    core::state::StructureClipboardState clipboard;
    macro::MacroAutomationBankState automation;
    macro::MacroAutomationSlotState emptySlot;

    assert(!clipboard.storeMacroAutomation(automation, emptySlot));
    assert(clipboard.kind.get() == core::state::StructureClipboardKind::NONE);

    std::cout << "[PASS] test_invalid_macro_automation_copy_reports_failure\n";
}

void test_macro_clipboards_store_only_the_selected_semantic_domain() {
    core::state::StructureClipboardState clipboard;
    macro::MacroPagesState pages;
    pages.setMacroSlotActive(0, true);
    pages.activePageData().cc[0] = 74;
    pages.updateActiveConfigs();
    const macro::MacroAutomationSlotAddress address{
        pages.currentActiveTrack(),
        pages.currentActivePage(),
        0,
    };
    auto* slot = macro::macroAutomationGetOrCreateSlot(pages.automation, address);
    assert(slot != nullptr);

    macro::MacroAutomationLane automation;
    assert(macro::macroAutomationAppendPoint(automation, 0.0f, 0.25f));
    assert(macro::macroAutomationAppendPoint(automation, 1.0f, 0.75f));
    assert(macro::macroAutomationAssignAutomation(
        pages.automation,
        *slot,
        automation
    ));
    macro::MacroModulationShape modulation;
    assert(macro::macroModulationAppendPoint(modulation, 0.0f, -0.25f));
    assert(macro::macroModulationAppendPoint(modulation, 1.0f, 0.25f));
    assert(macro::macroAutomationAssignModulation(
        pages.automation,
        *slot,
        modulation
    ));
    slot->modulationDepth = 0.6f;

    assert(clipboard.storeMacroAutomation(pages.automation, *slot));
    assert(clipboard.hasMacroAutomation());
    assert(clipboard.macroAutomationSet->payloadKind ==
           core::state::MacroClipboardPayloadKind::AUTOMATION);
    const auto& automationPayload =
        clipboard.macroAutomationSet->entries[0].state;
    assert(macro::macroCurveStored(automationPayload.automation));
    assert(!macro::macroCurveStored(automationPayload.modulation));

    assert(clipboard.storeMacroModulation(pages.automation, address));
    assert(clipboard.hasMacroModulation());
    assert(clipboard.macroAutomationSet->payloadKind ==
           core::state::MacroClipboardPayloadKind::MODULATION);
    const auto& modulationPayload =
        clipboard.macroAutomationSet->entries[0].state;
    assert(!macro::macroCurveStored(modulationPayload.automation));
    assert(macro::macroCurveStored(modulationPayload.modulation));
    assert(modulationPayload.modulationDepth == 0.6f);

    assert(clipboard.storeMacroDestination(pages, address));
    assert(clipboard.hasMacroDestination());
    assert(clipboard.macroAutomationSet->payloadKind ==
           core::state::MacroClipboardPayloadKind::DESTINATION);
    assert(clipboard.macroAutomationSet->sourceCc == 74);
    const auto& destinationPayload =
        clipboard.macroAutomationSet->entries[0].state;
    assert(!macro::macroCurveStored(destinationPayload.automation));
    assert(!macro::macroCurveStored(destinationPayload.modulation));

    std::cout
        << "[PASS] Macro clipboards contain only their selected domain\n";
}

}  // namespace

int main() {
    test_cross_domain_copy_releases_inactive_owned_payloads();
    test_rejected_copy_clears_previous_clipboard();
    test_invalid_macro_automation_copy_reports_failure();
    test_macro_clipboards_store_only_the_selected_semantic_domain();

    std::cout << "\nAll StructureClipboardState tests passed.\n";
    return 0;
}

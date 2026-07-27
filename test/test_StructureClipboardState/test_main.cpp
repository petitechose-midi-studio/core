#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <cmath>
#include <cstring>
#include <iostream>

#include "state/StructureClipboardState.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "../support/ProjectControlTestUtils.hpp"

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
    macro::MacroPagesState pages;
    assert(clipboard.storeMacroPage(macroPage, pages.control, 0, 0));
    assert(clipboard.hasMacroPage());
    assert(clipboard.sequencerGraph == nullptr);

    std::cout << "[PASS] test_cross_domain_copy_releases_inactive_owned_payloads\n";
}

void test_rejected_copy_clears_previous_clipboard() {
    core::state::StructureClipboardState clipboard;
    const macro::MacroAutomationSlotAddress address{};
    macro::MacroAutomationLane lane;
    lane.active = true;
    lane.durationBeats = 1.0f;
    assert(macro::macroAutomationAppendPoint(lane, 0.0f, 0.25f));
    assert(macro::macroAutomationAppendPoint(lane, 1.0f, 0.75f));
    macro::MacroPagesState pages;
    assert(test_support::project_control::assignAutomation(
        pages.control,
        address,
        lane
    ));
    assert(clipboard.storeMacroAutomation(pages.control, address));
    assert(clipboard.hasMacroAutomation());

    core::state::SequencerStepsClipboard emptySteps;
    assert(!clipboard.storeSequencerSteps(emptySteps, nullptr));
    assert(clipboard.kind.get() == core::state::StructureClipboardKind::NONE);
    assert(clipboard.macroAutomationSet == nullptr);
    assert(clipboard.sequencerGraph == nullptr);

    std::cout << "[PASS] test_rejected_copy_clears_previous_clipboard\n";
}

void test_invalid_macro_automation_copy_reports_failure() {
    core::state::StructureClipboardState clipboard;
    macro::MacroPagesState pages;
    assert(!clipboard.storeMacroAutomation(pages.control, {}));
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
    macro::MacroAutomationLane automation;
    assert(macro::macroAutomationAppendPoint(automation, 0.0f, 0.25f));
    assert(macro::macroAutomationAppendPoint(automation, 1.0f, 0.75f));
    assert(test_support::project_control::assignAutomation(
        pages.control,
        address,
        automation
    ));
    assert(core::state::modulation::setProjectControlAutomationEnabled(
        pages.control,
        address,
        false
    ));
    test_support::project_control::ModulationShape modulation;
    assert(test_support::project_control::appendModulationPoint(
        modulation, 0.0f, -0.25f
    ));
    assert(test_support::project_control::appendModulationPoint(
        modulation, 1.0f, 0.25f
    ));
    assert(test_support::project_control::assignModulation(
        pages.control,
        address,
        modulation,
        0.6f
    ));

    assert(clipboard.storeMacroAutomation(pages.control, address));
    assert(clipboard.hasMacroAutomation());
    assert(clipboard.macroAutomationSet->payloadKind ==
           core::state::MacroClipboardPayloadKind::AUTOMATION);
    const auto& automationPayload =
        clipboard.macroAutomationSet->entries[0].control;
    assert(automationPayload.automation.stored());
    assert(!automationPayload.automation.enabled);
    assert(!automationPayload.recordedShape.stored());

    assert(clipboard.storeMacroModulation(pages.control, address));
    assert(clipboard.hasMacroModulation());
    assert(clipboard.macroAutomationSet->payloadKind ==
           core::state::MacroClipboardPayloadKind::MODULATION);
    const auto& modulationPayload =
        clipboard.macroAutomationSet->entries[0].control;
    assert(!modulationPayload.automation.stored());
    assert(modulationPayload.recordedShape.stored());
    assert(std::fabs(modulationPayload.modulationAmount - 0.6f) < 0.0001f);

    assert(clipboard.storeMacroDestination(pages, address));
    assert(clipboard.hasMacroDestination());
    assert(clipboard.macroAutomationSet->payloadKind ==
           core::state::MacroClipboardPayloadKind::DESTINATION);
    assert(clipboard.macroAutomationSet->sourceCc == 74);
    const auto& destinationPayload =
        clipboard.macroAutomationSet->entries[0].control;
    assert(!destinationPayload.automation.stored());
    assert(!destinationPayload.recordedShape.stored());

    std::cout
        << "[PASS] Macro clipboards contain only their selected domain\n";
}

void test_modulation_assignment_clipboard_references_shared_source_only() {
    using namespace core::state::modulation;
    core::state::StructureClipboardState clipboard;
    macro::MacroPagesState pages;
    pages.setMacroSlotActive(0, true);
    const macro::MacroAutomationSlotAddress address{
        pages.currentActiveTrack(),
        pages.currentActivePage(),
        0,
    };
    ModulatorLfoDraft sourceDraft{};
    sourceDraft.name = "Shared LFO";
    sourceDraft.parameters.periodTicks = PROJECT_CONTROL_TICKS_PER_BEAT;
    const auto source = createLfoModulator(
        pages.control.authored.modulation,
        sourceDraft
    );
    assert(source.changed());
    ModulationBindingDraft bindingDraft{};
    bindingDraft.sourceId = source.sourceId;
    bindingDraft.destination = projectControlDestination(address);
    bindingDraft.amountQ15 = -12288;
    bindingDraft.application = ModulationApplication::AROUND_BASE;
    const auto binding = addProjectModulationBinding(
        pages.control.authored.modulation,
        bindingDraft
    );
    assert(binding.changed());

    assert(clipboard.storeMacroModulationAssignment(
        pages.control,
        address,
        binding.bindingId
    ));
    assert(clipboard.hasMacroModulationAssignment());
    assert(!clipboard.hasMacroModulation());
    assert(clipboard.macroAutomationSet == nullptr);
    assert(clipboard.macroModulationAssignment != nullptr);
    const auto& payload = *clipboard.macroModulationAssignment;
    assert(payload.sourceId == source.sourceId);
    assert(payload.binding.id == binding.bindingId);
    assert(payload.binding.amountQ15 == -12288);
    assert(std::strcmp(payload.sourceName.data(), "Shared LFO") == 0);

    assert(clipboard.storeMacroDestination(pages, address));
    assert(clipboard.macroModulationAssignment == nullptr);
    std::cout
        << "[PASS] Modulation assignment clipboard keeps one shared-source edge\n";
}

void test_project_modulator_source_clipboard_keeps_stable_reference() {
    using namespace core::state::modulation;
    core::state::StructureClipboardState clipboard;
    macro::MacroPagesState pages;

    ModulatorLfoDraft draft{};
    draft.name = "Shared Source";
    draft.parameters.periodTicks = PROJECT_CONTROL_TICKS_PER_BEAT;
    const auto created = createLfoModulator(
        pages.control.authored.modulation,
        draft
    );
    assert(created.changed());

    assert(clipboard.storeProjectModulatorSource(
        pages.control,
        created.sourceId
    ));
    assert(clipboard.hasProjectModulatorSource());
    assert(clipboard.kind.get() ==
           core::state::StructureClipboardKind::PROJECT_MODULATOR_SOURCE);
    assert(clipboard.projectModulatorSource.sourceId == created.sourceId);
    assert(clipboard.projectModulatorSource.kind == ModulatorKind::LFO);
    assert(std::strcmp(
        clipboard.projectModulatorSource.sourceName.data(),
        "Shared Source"
    ) == 0);

    clipboard.clear();
    assert(!clipboard.hasProjectModulatorSource());
    assert(!clipboard.projectModulatorSource.valid);
    std::cout
        << "[PASS] Project Source clipboard keeps one stable shared reference\n";
}

void test_macro_slot_selection_clipboard_keeps_sparse_full_slot_metadata() {
    core::state::StructureClipboardState clipboard;
    macro::MacroPagesState pages;
    auto& track = pages.tracks[0];
    track.enabledPageMask = 0x0003U;
    track.pages[0].setMacroActive(0U, true);
    track.pages[1].setMacroActive(3U, true);
    track.pages[0].cc[0] = 14U;
    track.pages[0].values[0] = 0.25f;
    track.pages[1].cc[3] = 74U;
    track.pages[1].values[3] = 0.75f;
    pages.syncActiveTrackCache();

    macro::MacroAutomationLane lane;
    assert(macro::macroAutomationAppendPoint(lane, 0.0f, 0.2f));
    assert(macro::macroAutomationAppendPoint(lane, 1.0f, 0.8f));
    assert(test_support::project_control::assignAutomation(
        pages.control,
        {.track = 0U, .page = 1U, .macro = 3U},
        lane
    ));

    oc::note::sequencer::StepBitMask128 selected{};
    selected.setBit(0U, true);
    selected.setBit(11U, true);
    assert(clipboard.storeMacroSlotSelection(
        pages,
        0U,
        selected
    ));
    assert(clipboard.hasMacroSlotSelection());
    assert(!clipboard.hasMacroSlot());
    assert(clipboard.macroAutomationSet != nullptr);
    const auto& payload = *clipboard.macroAutomationSet;
    assert(payload.count == 2U);
    assert(payload.entries[0].sourcePage == 0U);
    assert(payload.entries[0].sourceMacro == 0U);
    assert(payload.entries[0].sourceCc == 14U);
    assert(std::fabs(
        payload.entries[0].sourceStaticValue - 0.25f
    ) < 0.0001f);
    assert(payload.entries[1].sourcePage == 1U);
    assert(payload.entries[1].sourceMacro == 3U);
    assert(payload.entries[1].sourceCc == 74U);
    assert(std::fabs(
        payload.entries[1].sourceStaticValue - 0.75f
    ) < 0.0001f);
    assert(payload.entries[1].sourceSlotPresent);
    assert(payload.entries[1].control.automation.stored());

    std::cout
        << "[PASS] sparse Macro Slot clipboard keeps full entry metadata\n";
}

}  // namespace

int main() {
    test_cross_domain_copy_releases_inactive_owned_payloads();
    test_rejected_copy_clears_previous_clipboard();
    test_invalid_macro_automation_copy_reports_failure();
    test_macro_clipboards_store_only_the_selected_semantic_domain();
    test_modulation_assignment_clipboard_references_shared_source_only();
    test_project_modulator_source_clipboard_keeps_stable_reference();
    test_macro_slot_selection_clipboard_keeps_sparse_full_slot_metadata();

    std::cout << "\nAll StructureClipboardState tests passed.\n";
    return 0;
}

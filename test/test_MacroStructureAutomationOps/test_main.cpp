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

void assignShape(
    modulation::ProjectControlState& control,
    macro::MacroAutomationSlotAddress address,
    uint16_t scaleQ15
) {
    macro::MacroModulationShape shape{};
    shape.durationBeats = 2.0f;
    assert(macro::macroModulationAppendPoint(shape, 0.0f, -0.5f));
    assert(macro::macroModulationAppendPoint(shape, 1.0f, 0.5f));
    assert(test_support::project_control::assignModulation(
        control,
        address,
        shape,
        0.5f
    ));
    assert(modulation::setProjectModulationDestinationScale(
        control.authored.modulation,
        modulation::projectControlDestination(address),
        scaleQ15
    ).changed());
}

void test_track_duplication_preserves_automation_from_every_page() {
    modulation::ProjectControlState control;
    assignLane(control, {.track = 0, .page = 0, .macro = 1}, 2);
    assignLane(control, {.track = 0, .page = 7, .macro = 5}, 3);
    assignShape(control, {.track = 0, .page = 7, .macro = 5}, 49152U);

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
    assert(first.automationStored && first.compatibility.automation.pointCount == 2);
    assert(second.automationStored && second.compatibility.automation.pointCount == 3);
    assert(second.modulationStored);
    assert(modulation::projectModulationDestinationScaleQ15(
        control.authored.modulation,
        modulation::projectControlDestination({.track = 3, .page = 7, .macro = 5})
    ) == 49152U);

    std::cout
        << "[PASS] Project track duplication preserves every page automation\n";
}

void test_track_duplication_clones_local_adsr_and_remaps_its_note_route() {
    modulation::ProjectControlState control;
    const macro::MacroAutomationSlotAddress sourceAddress{
        .track = 0,
        .page = 1,
        .macro = 2,
    };
    modulation::ModulatorAdsrDraft source{};
    source.name = "Local ADSR";
    source.reach = {
        .kind = modulation::ModulatorReachKind::MACRO,
        .track = sourceAddress.track,
        .page = sourceAddress.page,
        .macro = sourceAddress.macro,
    };
    const auto created = modulation::createAdsrModulator(
        control.authored.modulation,
        source
    );
    assert(created.changed());
    modulation::ModulationBindingDraft binding{};
    binding.sourceId = created.sourceId;
    binding.destination = modulation::projectControlDestination(sourceAddress);
    binding.amountQ15 = 8192;
    assert(modulation::addProjectModulationBinding(
        control.authored.modulation,
        binding
    ).changed());
    modulation::ModulationTriggerDraft trigger{};
    trigger.sourceId = created.sourceId;
    trigger.trigger = {
        modulation::ModulationTriggerKind::TRACK_NOTE,
        sourceAddress.track,
        modulation::PROJECT_MODULATION_TRIGGER_ANY_CHANNEL,
        modulation::PROJECT_MODULATION_TRIGGER_ANY_NOTE,
    };
    assert(modulation::addProjectModulationTrigger(
        control.authored.modulation,
        trigger
    ).changed());

    const ops::ProjectControlTrackCopy copy{
        .sourceTrack = sourceAddress.track,
        .destTrack = 3U,
    };
    assert(ops::duplicateTracks(control, &copy, 1U));
    const auto& graph = control.authored.modulation;
    assert(graph.sourceCount == 2U);
    assert(graph.outputBindingCount == 2U);
    assert(graph.triggerBindingCount == 2U);
    assert(graph.sources[1].reach.track == 3U);
    assert(graph.outputBindings[1].destination.track == 3U);
    assert(graph.triggerBindings[1].sourceId == graph.sources[1].id);
    assert(graph.triggerBindings[1].trigger.track == 3U);
    assert(graph.triggerBindings[1].trigger.channel ==
           modulation::PROJECT_MODULATION_TRIGGER_ANY_CHANNEL);
    assert(modulation::validProjectModulationDomain(
        graph,
        control.authored.curves,
        &control.authored.automation
    ));
    std::cout << "[PASS] Track copy remaps a cloned local ADSR note route\n";
}

void test_track_clear_removes_note_route_without_deleting_root_source() {
    modulation::ProjectControlState control;
    modulation::ModulatorAdsrDraft source{};
    source.name = "Shared ADSR";
    source.reach.kind = modulation::ModulatorReachKind::PROJECT;
    const auto created = modulation::createAdsrModulator(
        control.authored.modulation,
        source
    );
    assert(created.changed());
    modulation::ModulationTriggerDraft trigger{};
    trigger.sourceId = created.sourceId;
    trigger.trigger = {
        modulation::ModulationTriggerKind::TRACK_NOTE,
        2U,
        modulation::PROJECT_MODULATION_TRIGGER_ANY_CHANNEL,
        modulation::PROJECT_MODULATION_TRIGGER_ANY_NOTE,
    };
    assert(modulation::addProjectModulationTrigger(
        control.authored.modulation,
        trigger
    ).changed());

    assert(ops::clearTracks(control, static_cast<uint16_t>(1U << 2U)));
    assert(control.authored.modulation.sourceCount == 1U);
    assert(control.authored.modulation.sources[0].id == created.sourceId);
    assert(control.authored.modulation.triggerBindingCount == 0U);
    std::cout << "[PASS] Track clear removes its route and keeps root ADSR\n";
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
    assignShape(sourceControl, {.track = 2, .page = 9, .macro = 6}, 16384U);
    core::state::StructureClipboardState clipboard;
    macro::MacroTrackData track;

    assert(clipboard.storeMacroTrack(track, sourceControl, 2));
    assert(clipboard.hasMacroTrack());
    assert(clipboard.macroAutomationSet != nullptr);
    assert(clipboard.macroAutomationSet->trackScope);
    assert(clipboard.macroAutomationSet->count == 2);
    assert(clipboard.macroAutomationSet->entries[1].destinationScaleQ15 ==
           16384U);

    modulation::ProjectControlState destinationControl;
    assert(ops::replaceTrackFromClipboard(
        destinationControl,
        5U,
        clipboard.macroAutomationSet.get()
    ));
    assert(modulation::projectModulationDestinationScaleQ15(
        destinationControl.authored.modulation,
        modulation::projectControlDestination({.track = 5, .page = 9, .macro = 6})
    ) == 16384U);

    std::cout << "[PASS] Track copy carries Project control and Global Depth\n";
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
    test_track_duplication_clones_local_adsr_and_remaps_its_note_route();
    test_track_clear_removes_note_route_without_deleting_root_source();
    test_batch_duplication_rejects_invalid_copy_atomically();
    test_empty_page_clipboard_replaces_existing_control_with_empty_scope();
    test_empty_structure_copy_does_not_allocate_control_clipboard();
    test_track_structure_copy_captures_all_page_automation();
    test_malformed_clipboard_is_rejected_before_destination_mutation();

    std::cout << "\nAll MacroStructureAutomationOps tests passed.\n";
    return 0;
}

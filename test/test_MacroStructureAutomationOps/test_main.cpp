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
    test_support::project_control::ModulationShape shape{};
    shape.durationBeats = 2.0f;
    assert(test_support::project_control::appendModulationPoint(
        shape, 0.0f, -0.5f
    ));
    assert(test_support::project_control::appendModulationPoint(
        shape, 1.0f, 0.5f
    ));
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

void test_track_clear_removes_note_route_without_deleting_root_source() {
    modulation::ProjectControlState control;
    modulation::ModulatorAdsrDraft source{};
    source.name = "Shared ADSR";
    const auto created = modulation::createAdsrModulator(
        control.authored.modulation,
        source
    );
    assert(created.changed());
    modulation::ModulationTriggerDraft trigger{};
    trigger.sourceId = created.sourceId;
    trigger.trigger = {
        .kind = modulation::ModulationTriggerKind::TRACK_NOTE,
        .track = 2U,
        .noteMin = 0U,
        .noteMax = 127U,
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

void test_page_compaction_remaps_complete_project_destinations() {
    modulation::ProjectControlState control;
    const macro::MacroAutomationSlotAddress removed{
        .track = 0,
        .page = 1,
        .macro = 0,
    };
    const macro::MacroAutomationSlotAddress shifted{
        .track = 0,
        .page = 2,
        .macro = 5,
    };
    const macro::MacroAutomationSlotAddress unrelated{
        .track = 4,
        .page = 2,
        .macro = 5,
    };
    assignLane(control, removed, 2U);
    assignLane(control, shifted, 3U);
    assignShape(control, shifted, 49152U);
    assignLane(control, unrelated, 2U);

    const auto shiftedBefore = test_support::project_control::readSlot(
        control,
        shifted
    );
    assert(shiftedBefore.automation.stored());
    const auto bindingId = control.authored.modulation.outputBindings[0].id;
    const auto sourceId = control.authored.modulation.outputBindings[0].sourceId;

    // Old Pages 0 and 2 survive. Page 2 becomes Page 1.
    assert(ops::compactPages(control, 0U, 0x0005U));

    const macro::MacroAutomationSlotAddress compacted{
        .track = 0,
        .page = 1,
        .macro = 5,
    };
    const auto shiftedAfter = test_support::project_control::readSlot(
        control,
        compacted
    );
    assert(shiftedAfter.automation.stored());
    assert(shiftedAfter.automation.id == shiftedBefore.automation.id);
    assert(shiftedAfter.modulationCount > 0U);
    assert(!test_support::project_control::readSlot(control, removed).present());
    assert(!test_support::project_control::readSlot(control, shifted).present());
    assert(test_support::project_control::readSlot(control, unrelated).present());
    assert(control.authored.modulation.outputBindings[0].id == bindingId);
    assert(control.authored.modulation.outputBindings[0].sourceId == sourceId);
    assert(control.authored.modulation.outputBindings[0].destination ==
           modulation::projectControlDestination(compacted));
    assert(modulation::projectModulationDestinationScaleQ15(
        control.authored.modulation,
        modulation::projectControlDestination(compacted)
    ) == 49152U);
    assert(modulation::validProjectModulationDomain(
        control.authored.modulation,
        control.authored.curves,
        &control.authored.automation
    ));

    std::cout << "[PASS] Page compaction remaps complete Project destinations\n";
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
    assert(!result.present());
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
    ).present());

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
    entry.control.automation.spec.valueDomain =
        modulation::ProjectCurveValueDomain::ABSOLUTE_UNIPOLAR;
    entry.control.automation.pointOffset =
        modulation::PROJECT_CURVE_POINT_CAPACITY;
    entry.control.automation.pointCount = 1U;
    entry.control.automation.enabled = true;

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
    test_track_clear_removes_note_route_without_deleting_root_source();
    test_page_compaction_remaps_complete_project_destinations();
    test_empty_page_clipboard_replaces_existing_control_with_empty_scope();
    test_empty_structure_copy_does_not_allocate_control_clipboard();
    test_track_structure_copy_captures_all_page_automation();
    test_malformed_clipboard_is_rejected_before_destination_mutation();

    std::cout << "\nAll MacroStructureAutomationOps tests passed.\n";
    return 0;
}

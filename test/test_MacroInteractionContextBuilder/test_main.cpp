#include <cassert>
#include <iostream>

#include "../../src/config/Timing.hpp"
#include "../../src/state/StructureClipboardState.hpp"
#include "../../src/state/TrackNavigationState.hpp"
#include "../../src/state/macro/MacroInteractionContextBuilder.hpp"
#include "../../src/state/macro/MacroPagesState.hpp"
#include "../../src/state/macro/MacroUiState.hpp"
#include "../support/ProjectControlTestUtils.hpp"

namespace {

using core::state::StructureNavigationFocus;
using core::state::macro::MacroInteractionContextSource;
using core::state::macro::MacroPagesState;

struct Harness {
    core::state::macro::MacroPagesState pages;
    core::state::macro::MacroUiState macroUi;
    core::state::TrackNavigationState trackNavigation;
    core::state::StructureClipboardState clipboard;
    uint16_t enabledTrackMask = MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;

    MacroInteractionContextSource source(
        StructureNavigationFocus focus = StructureNavigationFocus::PAGE,
        bool blockingOverlay = false,
        bool slotPropertySelecting = false
    ) const {
        return MacroInteractionContextSource{
            .pages = pages,
            .macroUi = macroUi,
            .trackNavigation = trackNavigation,
            .structureClipboard = clipboard,
            .navigationFocus = focus,
            .enabledTrackMask = enabledTrackMask,
            .blockingOverlay = blockingOverlay,
            .slotPropertySelecting = slotPropertySelecting,
        };
    }
};

void assignMinimalAutomation(core::state::macro::MacroPagesState& pages, uint8_t macroIndex) {
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = pages.currentActiveTrack(),
        .page = pages.currentActivePage(),
        .macro = macroIndex,
    };

    core::state::macro::MacroAutomationLane lane{};
    lane.active = true;
    lane.durationBeats = 1.0f;
    lane.pointCount = 2;
    lane.points[0] = core::state::macro::MacroCurvePoint{0.0f, 0.25f};
    lane.points[1] = core::state::macro::MacroCurvePoint{1.0f, 0.75f};
    assert(test_support::project_control::assignAutomation(
        pages.control,
        address,
        lane
    ));
}



void test_track_focus_uses_shared_track_mask_and_clipboard() {
    Harness h;
    h.enabledTrackMask = 0x0003;
    h.clipboard.kind.set(core::state::StructureClipboardKind::MACRO_TRACK);
    const auto context = core::state::macro::buildMacroInteractionContext(
        h.source(StructureNavigationFocus::TRACK)
    );
    assert(context.canRemoveStructure);
    assert(context.compatibleClipboardAvailable);

    h.trackNavigation.previewAddSlot.set(true);
    const auto preview = core::state::macro::buildMacroInteractionContext(
        h.source(StructureNavigationFocus::TRACK)
    );
    assert(preview.previewingAddSlot);
    assert(!preview.canRemoveStructure);

    std::cout << "[PASS] test_track_focus_uses_shared_track_mask_and_clipboard\n";
}

void test_track_focus_remains_first_class_on_hot_surface() {
    Harness h;
    h.enabledTrackMask = 0x0003;
    h.pages.setPageEnabled(1, true);
    h.clipboard.kind.set(core::state::StructureClipboardKind::MACRO_PAGE);

    const auto context = core::state::macro::buildMacroInteractionContext(
        h.source(StructureNavigationFocus::TRACK)
    );
    assert(context.navigationFocus == StructureNavigationFocus::TRACK);
    assert(!context.compatibleClipboardAvailable);
    assert(context.canRemoveStructure);

    std::cout
        << "[PASS] test_track_focus_remains_first_class_on_hot_surface\n";
}

void test_step_focus_requires_typed_slot_clipboard() {
    Harness h;
    h.macroUi.focusedMacroSlot.set(1);

    const auto addSlot = core::state::macro::buildMacroInteractionContext(
        h.source(StructureNavigationFocus::STEP)
    );
    assert(addSlot.previewingAddSlot);
    assert(!addSlot.compatibleClipboardAvailable);
    assert(!addSlot.canRemoveStructure);

    h.pages.setMacroSlotActive(1, true);
    h.macroUi.focusedMacroSlot.set(1);
    assignMinimalAutomation(h.pages, 1);
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = h.pages.currentActiveTrack(),
        .page = h.pages.currentActivePage(),
        .macro = 1,
    };
    assert(h.clipboard.storeMacroAutomation(h.pages.control, address));

    const auto storedAutomation = core::state::macro::buildMacroInteractionContext(
        h.source(StructureNavigationFocus::STEP)
    );
    assert(!storedAutomation.previewingAddSlot);
    assert(!storedAutomation.compatibleClipboardAvailable);
    assert(storedAutomation.canRemoveStructure);

    assert(h.clipboard.storeMacroSlot(
        h.pages,
        core::state::macro::MacroAutomationSlotAddress{
            .track = h.pages.currentActiveTrack(),
            .page = h.pages.currentActivePage(),
            .macro = 1,
        }
    ));
    assert(h.clipboard.hasMacroSlot());

    const auto activeSlot = core::state::macro::buildMacroInteractionContext(
        h.source(StructureNavigationFocus::STEP)
    );
    assert(!activeSlot.previewingAddSlot);
    assert(activeSlot.compatibleClipboardAvailable);
    assert(activeSlot.canRemoveStructure);

    std::cout << "[PASS] test_step_focus_requires_typed_slot_clipboard\n";
}

}  // namespace

int main() {
    test_track_focus_uses_shared_track_mask_and_clipboard();
    test_track_focus_remains_first_class_on_hot_surface();
    test_step_focus_requires_typed_slot_clipboard();
    std::cout << "\nAll MacroInteractionContextBuilder tests passed.\n";
    return 0;
}

#include <cassert>
#include <iostream>

#include "../../src/state/macro/MacroInteractionPolicy.hpp"

namespace {

using core::state::macro::MacroInteractionAction;
using core::state::macro::MacroInteractionContext;
using core::state::macro::MacroInteractionPolicy;
using core::state::macro::MacroInteractionVisibility;

void test_performance_mode_routes_structure_and_selectors() {
    MacroInteractionContext context{};

    assert(MacroInteractionPolicy::navTurn(context) ==
           MacroInteractionAction::MOVE_STRUCTURE);
    assert(MacroInteractionPolicy::navRelease(context, false) ==
           MacroInteractionAction::COMMIT_OR_CYCLE_STRUCTURE);
    assert(MacroInteractionPolicy::navLongPress(context) ==
           MacroInteractionAction::ENTER_SELECTION);
    assert(MacroInteractionPolicy::leftCenterPress(context) ==
           MacroInteractionAction::NONE);
    assert(MacroInteractionPolicy::leftBottomPress(context) ==
           MacroInteractionAction::OPEN_SLOT_PROPERTIES);
    assert(MacroInteractionPolicy::bottomLeftRelease(context) ==
           MacroInteractionAction::CLEAR_STRUCTURE);
    assert(MacroInteractionPolicy::bottomRightRelease(context) ==
           MacroInteractionAction::COPY_STRUCTURE);

    context.previewingAddSlot = true;
    assert(MacroInteractionPolicy::navRelease(context, false) ==
           MacroInteractionAction::CREATE_PREVIEWED_STRUCTURE);
    assert(MacroInteractionPolicy::bottomLeftRelease(context) ==
           MacroInteractionAction::NONE);

    std::cout << "[PASS] test_performance_mode_routes_structure_and_selectors\n";
}

void test_selector_modes_are_exclusive() {
    MacroInteractionContext context{};
    context.slotPropertySelecting = true;
    assert(MacroInteractionPolicy::navTurn(context) ==
           MacroInteractionAction::MOVE_SLOT_PROPERTY);
    assert(MacroInteractionPolicy::optTurn(context) ==
           MacroInteractionAction::EDIT_SLOT_PROPERTY);
    assert(MacroInteractionPolicy::leftBottomRelease(context) ==
           MacroInteractionAction::APPLY_SLOT_PROPERTIES);
    assert(MacroInteractionPolicy::leftCenterPress(context) ==
           MacroInteractionAction::NONE);
    assert(MacroInteractionPolicy::bottomRightRelease(context) ==
           MacroInteractionAction::NONE);

    std::cout << "[PASS] test_selector_modes_are_exclusive\n";
}

void test_macro_slot_focus_only_routes_navigation_and_property_selector() {
    MacroInteractionContext context{};
    context.navigationFocus = core::state::StructureNavigationFocus::STEP;

    assert(MacroInteractionPolicy::navTurn(context) ==
           MacroInteractionAction::MOVE_STRUCTURE);
    assert(MacroInteractionPolicy::navRelease(context, false) ==
           MacroInteractionAction::COMMIT_OR_CYCLE_STRUCTURE);
    assert(MacroInteractionPolicy::navLongPress(context) ==
           MacroInteractionAction::NONE);
    assert(MacroInteractionPolicy::leftBottomPress(context) ==
           MacroInteractionAction::OPEN_SLOT_PROPERTIES);
    assert(MacroInteractionPolicy::bottomLeftRelease(context) ==
           MacroInteractionAction::CLEAR_STRUCTURE);
    assert(MacroInteractionPolicy::bottomRightRelease(context) ==
           MacroInteractionAction::COPY_STRUCTURE);

    context.canRemoveStructure = true;
    assert(MacroInteractionPolicy::bottomLeftLongPress(context) ==
           MacroInteractionAction::REMOVE_STRUCTURE);

    context.compatibleClipboardAvailable = true;
    assert(MacroInteractionPolicy::bottomRightLongPress(context) ==
           MacroInteractionAction::PASTE_STRUCTURE);

    context.previewingAddSlot = true;
    context.compatibleClipboardAvailable = false;
    assert(MacroInteractionPolicy::navRelease(context, false) ==
           MacroInteractionAction::CREATE_PREVIEWED_STRUCTURE);
    assert(MacroInteractionPolicy::bottomLeftRelease(context) ==
           MacroInteractionAction::NONE);
    assert(MacroInteractionPolicy::bottomRightLongPress(context) ==
           MacroInteractionAction::NONE);

    const auto strip = MacroInteractionPolicy::actionStrip(context);
    assert(strip.bottomLeft == MacroInteractionVisibility::DIM);
    assert(strip.bottomRight == MacroInteractionVisibility::DIM);

    std::cout << "[PASS] test_macro_slot_focus_only_routes_navigation_and_property_selector\n";
}

void test_selection_and_blocking_overlay_contracts() {
    MacroInteractionContext context{};
    context.selectionActive = true;

    assert(MacroInteractionPolicy::navTurn(context) ==
           MacroInteractionAction::MOVE_SELECTION_CURSOR);
    assert(MacroInteractionPolicy::navRelease(context, false) ==
           MacroInteractionAction::TOGGLE_SELECTION);
    assert(MacroInteractionPolicy::leftTopRelease(context) ==
           MacroInteractionAction::CANCEL_SELECTION);
    assert(MacroInteractionPolicy::bottomLeftRelease(context) ==
           MacroInteractionAction::DELETE_SELECTION);
    assert(MacroInteractionPolicy::bottomRightRelease(context) ==
           MacroInteractionAction::DUPLICATE_SELECTION);

    context = {};
    context.blockingOverlay = true;
    assert(MacroInteractionPolicy::navTurn(context) == MacroInteractionAction::NONE);
    assert(MacroInteractionPolicy::leftCenterPress(context) == MacroInteractionAction::NONE);
    const auto strip = MacroInteractionPolicy::actionStrip(context);
    assert(strip.leftCenter == MacroInteractionVisibility::HIDDEN);
    assert(strip.bottomRight == MacroInteractionVisibility::HIDDEN);

    std::cout << "[PASS] test_selection_and_blocking_overlay_contracts\n";
}

}  // namespace

int main() {
    test_performance_mode_routes_structure_and_selectors();
    test_selector_modes_are_exclusive();
    test_macro_slot_focus_only_routes_navigation_and_property_selector();
    test_selection_and_blocking_overlay_contracts();
    std::cout << "\nAll MacroInteractionPolicy tests passed.\n";
    return 0;
}

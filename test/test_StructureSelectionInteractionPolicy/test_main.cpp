#include <cassert>
#include <iostream>

#include "state/StructureSelectionInteractionPolicy.hpp"

namespace {

using Action = core::state::StructureSelectionInteractionAction;
using Context = core::state::StructureSelectionInteractionContext;
using Phase = core::state::StructureSelectionPhase;
using Visibility = core::state::StructureSelectionInteractionVisibility;

void inactive_scope_only_exposes_explicit_entry() {
    auto policy =
        core::state::buildStructureSelectionInteractionPolicy(Context{});
    assert(policy.phase == Phase::INACTIVE);
    assert(policy.navLongPress == Action::NONE);
    assert(policy.navTurn == Action::NONE);
    assert(policy.leftTopRelease == Action::NONE);
    assert(policy.bottomRightVisibility == Visibility::HIDDEN);

    Context context{};
    context.entryAvailable = true;
    policy = core::state::buildStructureSelectionInteractionPolicy(context);
    assert(policy.navLongPress == Action::ENTER_SELECTION);
}

void empty_selection_can_move_toggle_or_exit() {
    Context context{};
    context.active = true;
    const auto policy =
        core::state::buildStructureSelectionInteractionPolicy(context);

    assert(policy.phase == Phase::SELECTING);
    assert(policy.navTurn == Action::MOVE_CURSOR);
    assert(policy.navRelease == Action::TOGGLE_ITEM);
    assert(policy.leftTopRelease == Action::EXIT_SELECTION);
    assert(policy.bottomRightRelease == Action::NONE);
    assert(policy.bottomRightLongPress == Action::NONE);
    assert(policy.bottomRightVisibility == Visibility::DISABLED);
}

void populated_selection_copies_and_clears_one_tier() {
    Context context{};
    context.active = true;
    context.selectedItemsAvailable = true;
    const auto policy =
        core::state::buildStructureSelectionInteractionPolicy(context);

    assert(policy.phase == Phase::SELECTING);
    assert(policy.leftTopRelease == Action::CLEAR_CURRENT);
    assert(policy.bottomRightRelease == Action::COPY_SELECTION);
    assert(policy.bottomRightLongPress == Action::NONE);
    assert(policy.bottomRightVisibility == Visibility::ACTIVE);
}

void placement_disables_toggle_and_requires_preflight_for_paste() {
    Context context{};
    context.active = true;
    context.placing = true;
    context.selectedItemsAvailable = true;

    auto policy =
        core::state::buildStructureSelectionInteractionPolicy(context);
    assert(policy.phase == Phase::PLACING);
    assert(policy.navTurn == Action::MOVE_CURSOR);
    assert(policy.navRelease == Action::NONE);
    assert(policy.leftTopRelease == Action::CLEAR_CURRENT);
    assert(policy.bottomRightRelease == Action::NONE);
    assert(policy.bottomRightLongPress == Action::NONE);
    assert(policy.bottomRightVisibility == Visibility::DISABLED);

    context.pasteAvailable = true;
    policy = core::state::buildStructureSelectionInteractionPolicy(context);
    assert(policy.bottomRightLongPress == Action::PASTE_SELECTION);
    assert(policy.bottomRightVisibility == Visibility::ACTIVE);
}

void inconsistent_inactive_flags_never_leak_actions() {
    Context context{};
    context.entryAvailable = true;
    context.placing = true;
    context.selectedItemsAvailable = true;
    context.pasteAvailable = true;
    const auto policy =
        core::state::buildStructureSelectionInteractionPolicy(context);

    assert(policy.phase == Phase::INACTIVE);
    assert(policy.navLongPress == Action::ENTER_SELECTION);
    assert(policy.navTurn == Action::NONE);
    assert(policy.navRelease == Action::NONE);
    assert(policy.leftTopRelease == Action::NONE);
    assert(policy.bottomRightRelease == Action::NONE);
    assert(policy.bottomRightLongPress == Action::NONE);
    assert(policy.bottomRightVisibility == Visibility::HIDDEN);
}

}  // namespace

int main() {
    inactive_scope_only_exposes_explicit_entry();
    empty_selection_can_move_toggle_or_exit();
    populated_selection_copies_and_clears_one_tier();
    placement_disables_toggle_and_requires_preflight_for_paste();
    inconsistent_inactive_flags_never_leak_actions();
    std::cout << "StructureSelectionInteractionPolicy tests passed\n";
    return 0;
}

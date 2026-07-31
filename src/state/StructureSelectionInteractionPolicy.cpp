#include "state/StructureSelectionInteractionPolicy.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM StructureSelectionInteractionPolicy
buildStructureSelectionInteractionPolicy(
    const StructureSelectionInteractionContext& context
) {
    using Action = StructureSelectionInteractionAction;
    using Phase = StructureSelectionPhase;
    using Visibility = StructureSelectionInteractionVisibility;

    StructureSelectionInteractionPolicy policy{};
    if (!context.active) {
        policy.navLongPress = context.entryAvailable
            ? Action::ENTER_SELECTION
            : Action::NONE;
        return policy;
    }

    policy.phase = context.placing ? Phase::PLACING : Phase::SELECTING;
    policy.navTurn = Action::MOVE_CURSOR;
    policy.leftTopRelease =
        context.placing || context.selectedItemsAvailable
        ? Action::CLEAR_CURRENT
        : Action::EXIT_SELECTION;

    if (context.placing) {
        policy.bottomRightLongPress = context.pasteAvailable
            ? Action::PASTE_SELECTION
            : Action::NONE;
        policy.bottomRightVisibility = context.pasteAvailable
            ? Visibility::ACTIVE
            : Visibility::DISABLED;
        return policy;
    }

    policy.navRelease = Action::TOGGLE_ITEM;
    policy.bottomRightRelease = context.selectedItemsAvailable
        ? Action::COPY_SELECTION
        : Action::NONE;
    policy.bottomRightVisibility = context.selectedItemsAvailable
        ? Visibility::ACTIVE
        : Visibility::DISABLED;
    return policy;
}

}  // namespace core::state

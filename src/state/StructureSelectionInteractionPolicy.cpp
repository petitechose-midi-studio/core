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

FLASHMEM core::state::interaction::ControllerIntent controllerIntentFor(
    StructureSelectionInteractionAction action
) {
    using Intent = core::state::interaction::ControllerIntent;
    switch (action) {
        case StructureSelectionInteractionAction::NONE:
            return Intent::NONE;
        case StructureSelectionInteractionAction::ENTER_SELECTION:
            return Intent::ENTER_SELECTION;
        case StructureSelectionInteractionAction::MOVE_CURSOR:
            return Intent::MOVE_FOCUS;
        case StructureSelectionInteractionAction::TOGGLE_ITEM:
            return Intent::ACTIVATE;
        case StructureSelectionInteractionAction::CLEAR_CURRENT:
        case StructureSelectionInteractionAction::EXIT_SELECTION:
            return Intent::BACK;
        case StructureSelectionInteractionAction::COPY_SELECTION:
            return Intent::COPY;
        case StructureSelectionInteractionAction::PASTE_SELECTION:
            return Intent::PASTE;
    }
    return Intent::NONE;
}

}  // namespace core::state

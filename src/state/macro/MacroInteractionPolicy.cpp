#include "state/macro/MacroInteractionPolicy.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

FLASHMEM bool MacroInteractionPolicy::performanceAvailable(
    const MacroInteractionContext& context
) {
    return !context.blockingOverlay &&
           !context.slotPropertySelecting &&
           !context.selectionActive;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::navTurn(
    const MacroInteractionContext& context
) {
    if (context.blockingOverlay) return MacroInteractionAction::NONE;
    if (context.selectionActive) return MacroInteractionAction::MOVE_SELECTION_CURSOR;
    if (context.slotPropertySelecting) return MacroInteractionAction::MOVE_SLOT_PROPERTY;
    return MacroInteractionAction::MOVE_STRUCTURE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::navRelease(
    const MacroInteractionContext& context,
    bool longPressConsumed
) {
    if (context.blockingOverlay || longPressConsumed) return MacroInteractionAction::NONE;
    if (context.selectionActive) return MacroInteractionAction::TOGGLE_SELECTION;
    if (context.slotPropertySelecting) {
        return MacroInteractionAction::NONE;
    }
    return context.previewingAddSlot
        ? MacroInteractionAction::CREATE_PREVIEWED_STRUCTURE
        : MacroInteractionAction::COMMIT_OR_CYCLE_STRUCTURE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::navLongPress(
    const MacroInteractionContext& context
) {
    if (context.navigationFocus == core::state::StructureNavigationFocus::STEP) {
        return MacroInteractionAction::NONE;
    }
    return performanceAvailable(context)
        ? MacroInteractionAction::ENTER_SELECTION
        : MacroInteractionAction::NONE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::optTurn(
    const MacroInteractionContext& context
) {
    if (context.blockingOverlay || context.selectionActive) return MacroInteractionAction::NONE;
    if (context.slotPropertySelecting) return MacroInteractionAction::EDIT_SLOT_PROPERTY;
    return MacroInteractionAction::NONE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::leftTopRelease(
    const MacroInteractionContext& context
) {
    if (context.blockingOverlay) return MacroInteractionAction::NONE;
    if (context.slotPropertySelecting) return MacroInteractionAction::CANCEL_SLOT_PROPERTIES;
    if (context.selectionActive) return MacroInteractionAction::CANCEL_SELECTION;
    return MacroInteractionAction::NONE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::leftCenterPress(
    const MacroInteractionContext& context
) {
    (void)context;
    return MacroInteractionAction::NONE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::leftBottomPress(
    const MacroInteractionContext& context
) {
    return performanceAvailable(context)
        ? MacroInteractionAction::OPEN_SLOT_PROPERTIES
        : MacroInteractionAction::NONE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::leftBottomRelease(
    const MacroInteractionContext& context
) {
    return !context.blockingOverlay && context.slotPropertySelecting
        ? MacroInteractionAction::APPLY_SLOT_PROPERTIES
        : MacroInteractionAction::NONE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::bottomLeftRelease(
    const MacroInteractionContext& context
) {
    if (context.blockingOverlay) return MacroInteractionAction::NONE;
    if (context.selectionActive) return MacroInteractionAction::DELETE_SELECTION;
    if (context.navigationFocus == core::state::StructureNavigationFocus::STEP) {
        return MacroInteractionAction::NONE;
    }
    if (!performanceAvailable(context) || context.previewingAddSlot) {
        return MacroInteractionAction::NONE;
    }
    return MacroInteractionAction::CLEAR_STRUCTURE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::bottomLeftLongPress(
    const MacroInteractionContext& context
) {
    if (context.navigationFocus == core::state::StructureNavigationFocus::STEP) {
        return MacroInteractionAction::NONE;
    }
    if (!performanceAvailable(context) || context.previewingAddSlot || !context.canRemoveStructure) {
        return MacroInteractionAction::NONE;
    }
    return MacroInteractionAction::REMOVE_STRUCTURE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::bottomRightRelease(
    const MacroInteractionContext& context
) {
    if (context.blockingOverlay) return MacroInteractionAction::NONE;
    if (context.selectionActive) return MacroInteractionAction::DUPLICATE_SELECTION;
    if (context.navigationFocus == core::state::StructureNavigationFocus::STEP) {
        return MacroInteractionAction::NONE;
    }
    if (!performanceAvailable(context) || context.previewingAddSlot) {
        return MacroInteractionAction::NONE;
    }
    return MacroInteractionAction::COPY_STRUCTURE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::bottomRightLongPress(
    const MacroInteractionContext& context
) {
    if (context.navigationFocus == core::state::StructureNavigationFocus::STEP) {
        return MacroInteractionAction::NONE;
    }
    if (!performanceAvailable(context) || !context.compatibleClipboardAvailable) {
        return MacroInteractionAction::NONE;
    }
    return MacroInteractionAction::PASTE_STRUCTURE;
}

FLASHMEM MacroActionStripPolicy MacroInteractionPolicy::actionStrip(
    const MacroInteractionContext& context
) {
    MacroActionStripPolicy policy;

    if (context.blockingOverlay) {
        policy.leftCenter = MacroInteractionVisibility::HIDDEN;
        policy.leftBottom = MacroInteractionVisibility::HIDDEN;
        policy.bottomLeft = MacroInteractionVisibility::HIDDEN;
        policy.bottomRight = MacroInteractionVisibility::HIDDEN;
        return policy;
    }

    if (context.selectionActive) {
        policy.leftCenter = MacroInteractionVisibility::HIDDEN;
        policy.leftBottom = MacroInteractionVisibility::HIDDEN;
        policy.bottomLeft = MacroInteractionVisibility::ACTIVE;
        policy.bottomRight = MacroInteractionVisibility::ACTIVE;
        return policy;
    }

    if (context.slotPropertySelecting) {
        policy.leftCenter = MacroInteractionVisibility::HIDDEN;
        policy.leftBottom = MacroInteractionVisibility::ACTIVE;
        policy.bottomLeft = MacroInteractionVisibility::HIDDEN;
        policy.bottomRight = MacroInteractionVisibility::HIDDEN;
        return policy;
    }

    policy.leftCenter = MacroInteractionVisibility::HIDDEN;
    policy.leftBottom = MacroInteractionVisibility::DIM;
    if (context.navigationFocus == core::state::StructureNavigationFocus::STEP) {
        policy.bottomLeft = MacroInteractionVisibility::HIDDEN;
        policy.bottomRight = MacroInteractionVisibility::HIDDEN;
        return policy;
    }
    policy.bottomLeft = context.previewingAddSlot
        ? MacroInteractionVisibility::DIM
        : MacroInteractionVisibility::ACTIVE;
    policy.bottomRight = MacroInteractionVisibility::ACTIVE;
    return policy;
}

}  // namespace core::state::macro

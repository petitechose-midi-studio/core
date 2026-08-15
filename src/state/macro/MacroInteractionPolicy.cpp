#include "state/macro/MacroInteractionPolicy.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

namespace {

FLASHMEM bool structureActionsAvailable(const MacroInteractionContext& context) {
    return !context.blockingOverlay &&
           !context.slotPropertySelecting;
}

}  // namespace

FLASHMEM bool MacroInteractionPolicy::performanceAvailable(
    const MacroInteractionContext& context
) {
    return !context.blockingOverlay &&
           !context.slotPropertySelecting;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::navTurn(
    const MacroInteractionContext& context
) {
    if (context.blockingOverlay) return MacroInteractionAction::NONE;
    if (context.slotPropertySelecting) return MacroInteractionAction::MOVE_SLOT_PROPERTY;
    return MacroInteractionAction::MOVE_STRUCTURE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::navRelease(
    const MacroInteractionContext& context
) {
    if (context.blockingOverlay) return MacroInteractionAction::NONE;
    if (context.slotPropertySelecting) {
        return MacroInteractionAction::NONE;
    }
    return context.previewingAddSlot
        ? MacroInteractionAction::CREATE_PREVIEWED_STRUCTURE
        : MacroInteractionAction::COMMIT_OR_CYCLE_STRUCTURE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::optTurn(
    const MacroInteractionContext& context
) {
    if (context.blockingOverlay) return MacroInteractionAction::NONE;
    if (context.slotPropertySelecting) return MacroInteractionAction::EDIT_SLOT_PROPERTY;
    return MacroInteractionAction::NONE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::leftTopRelease(
    const MacroInteractionContext& context
) {
    if (context.blockingOverlay) return MacroInteractionAction::NONE;
    if (context.slotPropertySelecting) return MacroInteractionAction::CANCEL_SLOT_PROPERTIES;
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
    if (!structureActionsAvailable(context) || context.previewingAddSlot) {
        return MacroInteractionAction::NONE;
    }
    if (context.navigationFocus == core::state::StructureNavigationFocus::STEP) {
        return context.canRemoveStructure
            ? MacroInteractionAction::CLEAR_STRUCTURE
            : MacroInteractionAction::NONE;
    }
    return MacroInteractionAction::CLEAR_STRUCTURE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::bottomLeftLongPress(
    const MacroInteractionContext& context
) {
    if (!structureActionsAvailable(context) || context.previewingAddSlot || !context.canRemoveStructure) {
        return MacroInteractionAction::NONE;
    }
    return MacroInteractionAction::REMOVE_STRUCTURE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::bottomRightRelease(
    const MacroInteractionContext& context
) {
    if (context.blockingOverlay) return MacroInteractionAction::NONE;
    if (!structureActionsAvailable(context) || context.previewingAddSlot) {
        return MacroInteractionAction::NONE;
    }
    return MacroInteractionAction::COPY_STRUCTURE;
}

FLASHMEM MacroInteractionAction MacroInteractionPolicy::bottomRightLongPress(
    const MacroInteractionContext& context
) {
    if (!structureActionsAvailable(context) || !context.compatibleClipboardAvailable) {
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

    if (context.slotPropertySelecting) {
        policy.leftCenter = MacroInteractionVisibility::HIDDEN;
        policy.leftBottom = MacroInteractionVisibility::ACTIVE;
        policy.bottomLeft = MacroInteractionVisibility::HIDDEN;
        policy.bottomRight = MacroInteractionVisibility::HIDDEN;
        return policy;
    }

    policy.leftCenter = MacroInteractionVisibility::HIDDEN;
    policy.leftBottom = MacroInteractionVisibility::DIM;

    policy.bottomLeft = context.previewingAddSlot
        ? MacroInteractionVisibility::DIM
        : MacroInteractionVisibility::ACTIVE;
    policy.bottomRight = context.previewingAddSlot &&
            !context.compatibleClipboardAvailable
        ? MacroInteractionVisibility::DIM
        : MacroInteractionVisibility::ACTIVE;
    return policy;
}

FLASHMEM core::state::interaction::ControllerIntent controllerIntentFor(
    MacroInteractionAction action
) {
    using Intent = core::state::interaction::ControllerIntent;
    switch (action) {
        case MacroInteractionAction::NONE:
            return Intent::NONE;
        case MacroInteractionAction::MOVE_STRUCTURE:
        case MacroInteractionAction::MOVE_SLOT_PROPERTY:
            return Intent::MOVE_FOCUS;
        case MacroInteractionAction::COMMIT_OR_CYCLE_STRUCTURE:
        case MacroInteractionAction::CREATE_PREVIEWED_STRUCTURE:
            return Intent::ACTIVATE;
        case MacroInteractionAction::OPEN_SLOT_PROPERTIES:
            return Intent::OPEN_ADVANCED;
        case MacroInteractionAction::APPLY_SLOT_PROPERTIES:
            return Intent::APPLY;
        case MacroInteractionAction::CANCEL_SLOT_PROPERTIES:
            return Intent::CANCEL;
        case MacroInteractionAction::EDIT_SLOT_PROPERTY:
            return Intent::EDIT_VALUE;
        case MacroInteractionAction::CLEAR_STRUCTURE:
            return Intent::RESET;
        case MacroInteractionAction::REMOVE_STRUCTURE:
            return Intent::DELETE_STRUCTURE;
        case MacroInteractionAction::COPY_STRUCTURE:
            return Intent::COPY;
        case MacroInteractionAction::PASTE_STRUCTURE:
            return Intent::PASTE;
    }
    return Intent::NONE;
}

}  // namespace core::state::macro

#include "state/sequencer/SequencerInteractionPolicy.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::sequencer {
namespace {

using Action = SequencerInteractionAction;
using Focus = StructureNavigationFocus;
using Scope = SequencerInteractionScope;
using Visibility = SequencerInteractionVisibility;

Visibility visibleIf(bool active) {
    return active ? Visibility::ACTIVE : Visibility::DISABLED;
}

void hideLeftSelectors(SequencerInteractionPolicy& policy);
void applyStructureBottomActions(
    SequencerInteractionPolicy& policy,
    const SequencerInteractionContext& context
);

void hideLeftSelectors(SequencerInteractionPolicy& policy) {
    policy.leftCenterVisibility = Visibility::HIDDEN;
    policy.leftBottomVisibility = Visibility::HIDDEN;
    policy.leftCenterPress = Action::NONE;
    policy.leftBottomPress = Action::NONE;
}

void disableMainEditing(SequencerInteractionPolicy& policy) {
    policy.navTap = Action::NONE;
    policy.navLongPress = Action::NONE;
    policy.optTurn = Action::NONE;
    policy.macroTap = Action::NONE;
    policy.macroLongPress = Action::NONE;
    policy.macroTurn = Action::NONE;
    policy.bottomLeftTap = Action::NONE;
    policy.bottomLeftHold = Action::NONE;
    policy.bottomRightTap = Action::NONE;
    policy.bottomRightHold = Action::NONE;
    policy.bottomLeftVisibility = Visibility::HIDDEN;
    policy.bottomRightVisibility = Visibility::HIDDEN;
}

void applyStructureBottomActions(SequencerInteractionPolicy& policy,
                                 const SequencerInteractionContext& context) {
    policy.bottomLeftTap = Action::CLEAR_CURRENT_STRUCTURE;
    policy.bottomLeftHold = Action::REMOVE_CURRENT_STRUCTURE;
    policy.bottomRightTap = Action::COPY_CURRENT_STRUCTURE;
    policy.bottomRightHold = Action::PASTE_CURRENT_STRUCTURE;
    policy.bottomLeftVisibility =
        visibleIf(context.currentStructureCanClear || context.currentStructureCanRemove);
    policy.bottomRightVisibility =
        visibleIf(context.currentStructureCanCopy || context.compatibleClipboardAvailable);
}

void applyStepContentBottomActions(SequencerInteractionPolicy& policy,
                                   const SequencerInteractionContext& context) {
    policy.bottomLeftTap = Action::CLEAR_STEP_CONTENT;
    policy.bottomLeftHold = Action::NONE;
    policy.bottomRightTap = Action::COPY_STEP_CONTENT;
    policy.bottomRightHold = Action::PASTE_STEP_CONTENT;
    policy.bottomLeftVisibility = visibleIf(context.currentStepHasChildContent);
    policy.bottomRightVisibility =
        visibleIf(context.currentStepHasChildContent || context.compatibleClipboardAvailable);
}

void applyStepBottomActions(SequencerInteractionPolicy& policy,
                            const SequencerInteractionContext& context) {
    (void)context;
    policy.bottomLeftTap = Action::RESET_CURRENT_STEP_SHALLOW;
    policy.bottomLeftHold = Action::RESET_CURRENT_STEP_DEEP;
    policy.bottomRightTap = Action::COPY_CURRENT_STEP;
    policy.bottomRightHold = Action::PASTE_CURRENT_STEP;
    policy.bottomLeftVisibility = Visibility::ACTIVE;
    policy.bottomRightVisibility = Visibility::ACTIVE;
}

SequencerInteractionPolicy buildSelectorPolicy(const SequencerInteractionContext& context,
                                               bool patternSelector) {
    SequencerInteractionPolicy policy{};
    const bool stepFocused =
        context.navigationFocus == core::state::StructureNavigationFocus::STEP;
    policy.scope = patternSelector ? Scope::PATTERN_DIMENSION_SELECTOR : Scope::MUSICAL_PROPERTY_SELECTOR;
    policy.navTurn = patternSelector ? Action::SELECT_PATTERN_DIMENSION : Action::SELECT_MUSICAL_PROPERTY;
    policy.navTap = patternSelector ? Action::APPLY_PATTERN_DIMENSION_SELECTOR
                                    : Action::APPLY_MUSICAL_PROPERTY_SELECTOR;
    policy.navLongPress = Action::NONE;
    policy.optTurn = patternSelector ? Action::EDIT_PATTERN_DIMENSION
                                     : Action::EDIT_MUSICAL_PROPERTY_VARIATION;
    policy.leftTopTap = Action::CANCEL_TRANSIENT_CONTEXT;
    policy.leftCenterPress = patternSelector
        ? Action::APPLY_PATTERN_DIMENSION_SELECTOR
        : stepFocused ? Action::APPLY_MUSICAL_PROPERTY_SELECTOR : Action::NONE;
    policy.leftBottomPress = patternSelector
        ? Action::NONE
        : stepFocused ? Action::EDIT_STEP_LOCAL_RANDOM : Action::APPLY_MUSICAL_PROPERTY_SELECTOR;
    policy.macroTap = Action::NONE;
    policy.macroLongPress = Action::NONE;
    policy.macroTurn = patternSelector ? Action::EDIT_PATTERN_DIMENSION
                                       : Action::EDIT_MUSICAL_PROPERTY_VARIATION;
    policy.bottomLeftTap = Action::NONE;
    policy.bottomLeftHold = Action::NONE;
    policy.bottomRightTap = Action::NONE;
    policy.bottomRightHold = Action::NONE;
    policy.leftCenterVisibility = patternSelector || stepFocused ? Visibility::ACTIVE
                                                                 : Visibility::HIDDEN;
    policy.leftBottomVisibility = patternSelector ? Visibility::HIDDEN : Visibility::ACTIVE;
    policy.bottomLeftVisibility = Visibility::HIDDEN;
    policy.bottomRightVisibility = Visibility::HIDDEN;
    return policy;
}

SequencerInteractionPolicy buildStepContentSelectorPolicy() {
    SequencerInteractionPolicy policy{};
    policy.scope = Scope::STEP_CONTENT_SELECTOR;
    policy.navTurn = Action::SELECT_STEP_CONTENT_ACTION;
    policy.navTap = Action::APPLY_STEP_CONTENT_SELECTOR;
    policy.navLongPress = Action::NONE;
    policy.optTurn = Action::NONE;
    policy.leftTopTap = Action::CANCEL_TRANSIENT_CONTEXT;
    policy.leftCenterPress = Action::NONE;
    policy.leftBottomPress = Action::APPLY_STEP_CONTENT_SELECTOR;
    policy.macroTap = Action::NONE;
    policy.macroLongPress = Action::NONE;
    policy.macroTurn = Action::NONE;
    policy.bottomLeftTap = Action::NONE;
    policy.bottomLeftHold = Action::NONE;
    policy.bottomRightTap = Action::NONE;
    policy.bottomRightHold = Action::NONE;
    policy.leftCenterVisibility = Visibility::HIDDEN;
    policy.leftBottomVisibility = Visibility::ACTIVE;
    policy.bottomLeftVisibility = Visibility::HIDDEN;
    policy.bottomRightVisibility = Visibility::HIDDEN;
    return policy;
}

SequencerInteractionPolicy buildSelectionPolicy(const SequencerInteractionContext& context) {
    SequencerInteractionPolicy policy{};
    if (context.stepSelectionActive) {
        policy.scope = Scope::STEP_SELECTION;
        policy.bottomLeftTap = Action::RESET_STEP_SELECTION_SHALLOW;
        policy.bottomLeftHold = Action::RESET_STEP_SELECTION_DEEP;
        policy.bottomRightTap = Action::COPY_STEP_SELECTION;
        policy.bottomRightHold = Action::PASTE_STEP_SELECTION;
    } else if (context.trackSelectionActive) {
        policy.scope = Scope::TRACK_SELECTION;
        policy.bottomLeftTap = Action::MUTE_TRACK_SELECTION;
        policy.bottomLeftHold = Action::DELETE_SELECTION;
        policy.bottomRightTap = Action::NONE;
        policy.bottomRightHold = Action::NONE;
    } else {
        policy.scope = Scope::PATTERN_SELECTION;
        policy.bottomLeftTap = Action::CLEAR_SELECTION;
        policy.bottomLeftHold = Action::DELETE_SELECTION;
        policy.bottomRightTap = Action::NONE;
        policy.bottomRightHold = Action::NONE;
    }

    policy.navTurn = Action::MOVE_SELECTION_CURSOR;
    policy.navTap = Action::TOGGLE_SELECTION;
    policy.navLongPress = Action::NONE;
    policy.optTurn = Action::NONE;
    policy.leftTopTap = Action::CANCEL_TRANSIENT_CONTEXT;
    policy.macroTap = Action::TOGGLE_SELECTION;
    policy.macroLongPress = Action::NONE;
    policy.macroTurn = Action::NONE;
    policy.bottomLeftVisibility = visibleIf(context.selectedItemsAvailable);
    policy.bottomRightVisibility = context.stepSelectionActive
        ? visibleIf(
              context.selectedItemsAvailable ||
              context.compatibleClipboardAvailable
          )
        : Visibility::HIDDEN;
    hideLeftSelectors(policy);
    return policy;
}

SequencerInteractionPolicy buildStepEditorPolicy(const SequencerInteractionContext& context) {
    SequencerInteractionPolicy policy{};
    const bool canRetargetStep = !context.childContentView && !context.overlayVisible;
    policy.scope = Scope::STEP_EDITOR;
    policy.navTurn = Action::SELECT_STEP_EDITOR_ROW;
    policy.navTap = Action::APPLY_STEP_EDITOR;
    policy.navLongPress = Action::NONE;
    policy.optTurn = Action::EDIT_STEP_EDITOR_ROW;
    policy.leftTopTap = Action::CANCEL_TRANSIENT_CONTEXT;
    policy.leftCenterPress = canRetargetStep
        ? Action::RETARGET_STEP_EDITOR
        : Action::NONE;
    policy.leftBottomPress = Action::EDIT_STEP_LOCAL_RANDOM;
    policy.macroTap = Action::NONE;
    policy.macroLongPress = Action::NONE;
    policy.macroTurn = Action::EDIT_STEP_EDITOR_ROW;
    policy.bottomLeftTap = Action::NONE;
    policy.bottomLeftHold = Action::NONE;
    policy.bottomRightTap = Action::NONE;
    policy.bottomRightHold = Action::NONE;
    policy.bottomLeftVisibility = Visibility::HIDDEN;
    policy.bottomRightVisibility = Visibility::HIDDEN;

    if (context.stepEditorValueRowFocused) {
        policy.bottomLeftTap = Action::RESET_STEP_EDITOR_ROW;
        policy.bottomLeftVisibility = Visibility::ACTIVE;
    } else if (context.stepEditorContextRowFocused) {
        policy.bottomLeftHold = Action::REMOVE_STEP_EDITOR_CONTEXT;
        policy.bottomRightTap = Action::COPY_STEP_EDITOR_CONTEXT;
        policy.bottomRightHold = Action::PASTE_STEP_EDITOR_CONTEXT;
        policy.bottomLeftVisibility = visibleIf(context.stepEditorContextHasChild);
        policy.bottomRightVisibility =
            visibleIf(context.stepEditorContextHasChild || context.compatibleClipboardAvailable);
    }

    policy.leftCenterVisibility = canRetargetStep
        ? Visibility::ACTIVE
        : Visibility::HIDDEN;
    policy.leftBottomVisibility = Visibility::ACTIVE;
    return policy;
}

SequencerInteractionPolicy buildMainSurfacePolicy(const SequencerInteractionContext& context) {
    SequencerInteractionPolicy policy{};
    const bool childContentView = context.childContentView;

    switch (context.navigationFocus) {
        case Focus::STEP:
            policy.scope = Scope::STEP;
            policy.navTurn = Action::MOVE_STEP;
            policy.navTap = Action::OPEN_STEP_EDITOR;
            policy.navLongPress = Action::ENTER_SELECTION;
            policy.optTurn = Action::EDIT_STEP_PROPERTY;
            policy.leftTopTap = childContentView
                ? Action::CANCEL_TRANSIENT_CONTEXT
                : Action::NONE;
            policy.leftCenterPress = Action::OPEN_MUSICAL_PROPERTY_SELECTOR;
            policy.leftCenterVisibility = Visibility::ACTIVE;
            policy.leftBottomPress = Action::OPEN_STEP_CONTENT_SELECTOR;
            policy.leftBottomVisibility = Visibility::ACTIVE;
            applyStepBottomActions(policy, context);
            break;

        case Focus::TRACK:
            policy.scope = Scope::TRACK;
            policy.navTurn = Action::MOVE_TRACK;
            policy.navTap = Action::OPEN_TRACK_EDITOR;
            policy.navLongPress = Action::ENTER_SELECTION;
            policy.optTurn = Action::NONE;
            policy.leftTopTap = Action::NONE;
            policy.leftCenterPress = Action::NONE;
            policy.leftCenterVisibility = Visibility::HIDDEN;
            policy.leftBottomPress = Action::NONE;
            policy.leftBottomVisibility = Visibility::HIDDEN;
            applyStructureBottomActions(policy, context);
            break;

        case Focus::PAGE:
        default:
            policy.scope = childContentView ? Scope::CHILD_PATTERN : Scope::PATTERN;
            policy.navTurn = Action::MOVE_PATTERN;
            policy.navTap = childContentView
                ? Action::NONE
                : Action::OPEN_PATTERN_EDITOR;
            policy.navLongPress = Action::ENTER_SELECTION;
            policy.optTurn = Action::EDIT_PATTERN_DIMENSION;
            policy.leftTopTap = childContentView
                ? Action::CANCEL_TRANSIENT_CONTEXT
                : Action::NONE;
            policy.leftCenterPress = Action::OPEN_PATTERN_DIMENSION_SELECTOR;
            policy.leftBottomPress = Action::OPEN_MUSICAL_PROPERTY_SELECTOR;
            policy.leftCenterVisibility = Visibility::ACTIVE;
            policy.leftBottomVisibility = Visibility::ACTIVE;
            if (childContentView) {
                applyStepContentBottomActions(policy, context);
            } else {
                applyStructureBottomActions(policy, context);
            }
            break;
    }

    if (context.overlayVisible) {
        disableMainEditing(policy);
        hideLeftSelectors(policy);
    }

    return policy;
}

}  // namespace

FLASHMEM bool sequencerInteractionSelectionActive(const SequencerInteractionContext& context) {
    return context.trackSelectionActive ||
           context.pageSelectionActive ||
           context.stepSelectionActive;
}

FLASHMEM bool sequencerInteractionTransientActive(const SequencerInteractionContext& context) {
    return context.overlayVisible || context.patternQuickControlsActive ||
           context.propertySelectorActive || context.stepContentSelectorActive ||
           context.stepEditorVisible;
}

FLASHMEM bool sequencerInteractionMainSurfaceAvailable(const SequencerInteractionContext& context) {
    return !sequencerInteractionSelectionActive(context) && !sequencerInteractionTransientActive(context);
}

FLASHMEM SequencerInteractionPolicy buildSequencerInteractionPolicy(
    const SequencerInteractionContext& context
) {
    if (context.stepEditorVisible) {
        return buildStepEditorPolicy(context);
    }
    if (context.patternQuickControlsActive) {
        return buildSelectorPolicy(context, true);
    }
    if (context.propertySelectorActive) {
        return buildSelectorPolicy(context, false);
    }
    if (context.stepContentSelectorActive) {
        return buildStepContentSelectorPolicy();
    }
    if (sequencerInteractionSelectionActive(context)) {
        return buildSelectionPolicy(context);
    }
    return buildMainSurfacePolicy(context);
}

}  // namespace core::state::sequencer

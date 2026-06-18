#include "state/sequencer/SequencerInteractionPolicy.hpp"

namespace core::state::sequencer {
namespace {

using Action = SequencerInteractionAction;
using Focus = StructureNavigationFocus;
using Scope = SequencerInteractionScope;
using Visibility = SequencerInteractionVisibility;

Visibility visibleIf(bool active) {
    return active ? Visibility::ACTIVE : Visibility::DISABLED;
}

Action cycleOrCreatePreview(const SequencerInteractionContext& context) {
    return context.previewingAddSlot ? Action::CREATE_PREVIEW_STRUCTURE : Action::CYCLE_SCOPE;
}

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

SequencerInteractionPolicy buildSelectorPolicy(bool patternSelector) {
    SequencerInteractionPolicy policy{};
    policy.scope = patternSelector ? Scope::PATTERN_DIMENSION_SELECTOR : Scope::MUSICAL_PROPERTY_SELECTOR;
    policy.navTurn = patternSelector ? Action::SELECT_PATTERN_DIMENSION : Action::SELECT_MUSICAL_PROPERTY;
    policy.navTap = patternSelector ? Action::APPLY_PATTERN_DIMENSION_SELECTOR
                                    : Action::APPLY_MUSICAL_PROPERTY_SELECTOR;
    policy.navLongPress = Action::NONE;
    policy.optTurn = patternSelector ? Action::EDIT_PATTERN_DIMENSION
                                     : Action::EDIT_MUSICAL_PROPERTY_VARIATION;
    policy.leftTopTap = Action::CANCEL_TRANSIENT_CONTEXT;
    policy.leftCenterPress = patternSelector ? Action::APPLY_PATTERN_DIMENSION_SELECTOR : Action::NONE;
    policy.leftBottomPress = patternSelector ? Action::NONE : Action::APPLY_MUSICAL_PROPERTY_SELECTOR;
    policy.macroTap = Action::NONE;
    policy.macroLongPress = Action::NONE;
    policy.macroTurn = patternSelector ? Action::EDIT_PATTERN_DIMENSION
                                       : Action::EDIT_MUSICAL_PROPERTY_VARIATION;
    policy.bottomLeftTap = Action::NONE;
    policy.bottomLeftHold = Action::NONE;
    policy.bottomRightTap = Action::NONE;
    policy.bottomRightHold = Action::NONE;
    policy.leftCenterVisibility = patternSelector ? Visibility::ACTIVE : Visibility::HIDDEN;
    policy.leftBottomVisibility = patternSelector ? Visibility::HIDDEN : Visibility::ACTIVE;
    policy.bottomLeftVisibility = Visibility::HIDDEN;
    policy.bottomRightVisibility = Visibility::HIDDEN;
    return policy;
}

SequencerInteractionPolicy buildSelectionPolicy(const SequencerInteractionContext& context) {
    SequencerInteractionPolicy policy{};
    if (context.stepSelectionActive) {
        policy.scope = Scope::STEP_SELECTION;
        policy.bottomLeftTap = Action::CLEAR_SELECTION;
        policy.bottomLeftHold = Action::DELETE_SELECTION;
        policy.bottomRightTap = Action::COPY_STEP_SELECTION;
        policy.bottomRightHold = Action::PASTE_STEP_SELECTION;
    } else if (context.trackSelectionActive) {
        policy.scope = Scope::TRACK_SELECTION;
        policy.bottomLeftTap = Action::CLEAR_SELECTION;
        policy.bottomLeftHold = Action::DELETE_SELECTION;
        policy.bottomRightTap = Action::DUPLICATE_SELECTION;
        policy.bottomRightHold = Action::NONE;
    } else {
        policy.scope = Scope::PATTERN_SELECTION;
        policy.bottomLeftTap = Action::CLEAR_SELECTION;
        policy.bottomLeftHold = Action::DELETE_SELECTION;
        policy.bottomRightTap = Action::DUPLICATE_SELECTION;
        policy.bottomRightHold = Action::NONE;
    }

    policy.navTurn = Action::MOVE_SELECTION_CURSOR;
    policy.navTap = Action::TOGGLE_SELECTION;
    policy.navLongPress = Action::CANCEL_TRANSIENT_CONTEXT;
    policy.optTurn = Action::NONE;
    policy.leftTopTap = Action::CANCEL_TRANSIENT_CONTEXT;
    policy.macroTap = Action::TOGGLE_SELECTION;
    policy.macroLongPress = Action::NONE;
    policy.macroTurn = Action::NONE;
    policy.bottomLeftVisibility = visibleIf(context.selectedItemsAvailable);
    policy.bottomRightVisibility =
        visibleIf(context.selectedItemsAvailable || context.compatibleClipboardAvailable);
    hideLeftSelectors(policy);
    return policy;
}

SequencerInteractionPolicy buildStepEditorPolicy(const SequencerInteractionContext& context) {
    SequencerInteractionPolicy policy{};
    policy.scope = Scope::STEP_EDITOR;
    policy.navTurn = Action::SELECT_STEP_EDITOR_ROW;
    policy.navTap = Action::APPLY_STEP_EDITOR;
    policy.navLongPress = Action::NONE;
    policy.optTurn = Action::EDIT_STEP_EDITOR_ROW;
    policy.leftTopTap = Action::CANCEL_TRANSIENT_CONTEXT;
    policy.leftCenterPress = Action::NONE;
    policy.leftBottomPress = Action::EDIT_STEP_LOCAL_RANDOM;
    policy.macroTap = Action::NONE;
    policy.macroLongPress = Action::NONE;
    policy.macroTurn = Action::EDIT_STEP_EDITOR_ROW;
    policy.bottomLeftTap = Action::NONE;
    policy.bottomLeftHold = Action::NONE;
    policy.bottomRightTap = Action::NONE;
    policy.bottomRightHold = Action::NONE;
    policy.leftCenterVisibility = Visibility::HIDDEN;
    policy.leftBottomVisibility = Visibility::ACTIVE;
    policy.bottomLeftVisibility = Visibility::HIDDEN;
    policy.bottomRightVisibility = visibleIf(context.compatibleClipboardAvailable);
    return policy;
}

SequencerInteractionPolicy buildMainSurfacePolicy(const SequencerInteractionContext& context) {
    SequencerInteractionPolicy policy{};
    const bool childContentView = context.childContentView;

    switch (context.navigationFocus) {
        case Focus::TRACK:
            policy.scope = Scope::TRACK_LEGACY;
            policy.navTurn = Action::MOVE_TRACK;
            policy.navTap = cycleOrCreatePreview(context);
            policy.navLongPress = Action::ENTER_SELECTION;
            policy.optTurn = Action::NONE;
            policy.macroTap = Action::NONE;
            policy.macroTurn = Action::NONE;
            hideLeftSelectors(policy);
            applyStructureBottomActions(policy, context);
            break;

        case Focus::STEP:
            policy.scope = Scope::STEP;
            policy.navTurn = Action::MOVE_STEP;
            policy.navTap = cycleOrCreatePreview(context);
            policy.navLongPress = Action::ENTER_SELECTION;
            policy.optTurn = Action::EDIT_STEP_PROPERTY;
            policy.leftCenterPress = Action::NONE;
            policy.leftCenterVisibility = Visibility::HIDDEN;
            policy.leftBottomPress = Action::OPEN_MUSICAL_PROPERTY_SELECTOR;
            policy.leftBottomVisibility = Visibility::ACTIVE;
            if (childContentView) {
                applyStepContentBottomActions(policy, context);
            } else {
                applyStructureBottomActions(policy, context);
            }
            break;

        case Focus::PAGE:
        default:
            policy.scope = childContentView ? Scope::CHILD_PATTERN : Scope::PATTERN;
            policy.navTurn = Action::MOVE_PATTERN;
            policy.navTap = cycleOrCreatePreview(context);
            policy.navLongPress = Action::ENTER_SELECTION;
            policy.optTurn = Action::EDIT_PATTERN_DIMENSION;
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

bool sequencerInteractionSelectionActive(const SequencerInteractionContext& context) {
    return context.pageSelectionActive || context.trackSelectionActive || context.stepSelectionActive;
}

bool sequencerInteractionTransientActive(const SequencerInteractionContext& context) {
    return context.overlayVisible || context.patternQuickControlsActive || context.propertySelectorActive ||
           context.stepEditorVisible;
}

bool sequencerInteractionMainSurfaceAvailable(const SequencerInteractionContext& context) {
    return !sequencerInteractionSelectionActive(context) && !sequencerInteractionTransientActive(context);
}

SequencerInteractionPolicy buildSequencerInteractionPolicy(const SequencerInteractionContext& context) {
    if (context.stepEditorVisible) {
        return buildStepEditorPolicy(context);
    }
    if (context.patternQuickControlsActive) {
        return buildSelectorPolicy(true);
    }
    if (context.propertySelectorActive) {
        return buildSelectorPolicy(false);
    }
    if (sequencerInteractionSelectionActive(context)) {
        return buildSelectionPolicy(context);
    }
    return buildMainSurfacePolicy(context);
}

}  // namespace core::state::sequencer

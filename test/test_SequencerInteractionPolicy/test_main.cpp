#include <cassert>
#include <cstring>
#include <iostream>

#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "ui/sequencer/SequencerActionStripVisuals.hpp"

using namespace core::state;
using namespace core::state::sequencer;

namespace {

using Action = SequencerInteractionAction;
using Focus = StructureNavigationFocus;
using Scope = SequencerInteractionScope;
using Visibility = SequencerInteractionVisibility;

SequencerInteractionContext baseContext(Focus focus = Focus::PAGE) {
    SequencerInteractionContext context{};
    context.navigationFocus = focus;
    return context;
}

void expectsRootFocusMatrix() {
    auto track = buildSequencerInteractionPolicy(baseContext(Focus::TRACK));
    assert(track.scope == Scope::TRACK);
    assert(track.navTurn == Action::MOVE_TRACK);
    assert(track.optTurn == Action::NONE);
    assert(track.macroLongPress == Action::OPEN_STEP_EDITOR);
    assert(track.bottomLeftTap == Action::MUTE_CURRENT_TRACK);
    assert(track.bottomLeftHold == Action::REMOVE_CURRENT_STRUCTURE);
    assert(track.leftCenterVisibility == Visibility::HIDDEN);
    assert(track.leftBottomVisibility == Visibility::HIDDEN);

    auto pattern = buildSequencerInteractionPolicy(baseContext(Focus::PAGE));
    assert(pattern.scope == Scope::PATTERN);
    assert(pattern.navTurn == Action::MOVE_PATTERN);
    assert(pattern.optTurn == Action::EDIT_PATTERN_DIMENSION);
    assert(pattern.macroLongPress == Action::OPEN_STEP_EDITOR);
    assert(pattern.leftCenterPress == Action::OPEN_PATTERN_DIMENSION_SELECTOR);
    assert(pattern.leftBottomPress == Action::OPEN_MUSICAL_PROPERTY_SELECTOR);
    assert(pattern.leftCenterVisibility == Visibility::ACTIVE);
    assert(pattern.leftBottomVisibility == Visibility::ACTIVE);

    auto step = buildSequencerInteractionPolicy(baseContext(Focus::STEP));
    assert(step.scope == Scope::STEP);
    assert(step.navTurn == Action::MOVE_STEP);
    assert(step.optTurn == Action::EDIT_STEP_PROPERTY);
    assert(step.macroLongPress == Action::OPEN_STEP_EDITOR);
    assert(step.leftCenterPress == Action::OPEN_MUSICAL_PROPERTY_SELECTOR);
    assert(step.leftBottomPress == Action::NONE);
    assert(step.leftCenterVisibility == Visibility::ACTIVE);
    assert(step.leftBottomVisibility == Visibility::HIDDEN);
}

void expectsChildContentBottomActions() {
    auto context = baseContext(Focus::PAGE);
    context.childContentView = true;
    context.currentStepHasChildContent = true;
    context.compatibleClipboardAvailable = true;

    auto policy = buildSequencerInteractionPolicy(context);
    assert(policy.scope == Scope::CHILD_PATTERN);
    assert(policy.macroLongPress == Action::OPEN_STEP_EDITOR);
    assert(policy.bottomLeftTap == Action::CLEAR_STEP_CONTENT);
    assert(policy.bottomRightTap == Action::COPY_STEP_CONTENT);
    assert(policy.bottomRightHold == Action::PASTE_STEP_CONTENT);
    assert(policy.bottomLeftVisibility == Visibility::ACTIVE);
    assert(policy.bottomRightVisibility == Visibility::ACTIVE);

    context.navigationFocus = Focus::STEP;
    policy = buildSequencerInteractionPolicy(context);
    assert(policy.scope == Scope::STEP);
    assert(policy.optTurn == Action::EDIT_STEP_PROPERTY);
    assert(policy.macroLongPress == Action::OPEN_STEP_EDITOR);
    assert(policy.bottomLeftTap == Action::RESET_CURRENT_STEP_SHALLOW);
    assert(policy.bottomLeftHold == Action::RESET_CURRENT_STEP_DEEP);
    assert(policy.bottomRightTap == Action::COPY_CURRENT_STEP);
    assert(policy.bottomRightHold == Action::PASTE_CURRENT_STEP);
}

void expectsStructureCopyVisibleWithoutClipboard() {
    auto context = baseContext(Focus::PAGE);
    context.currentStructureCanCopy = true;
    context.compatibleClipboardAvailable = false;

    auto policy = buildSequencerInteractionPolicy(context);
    assert(policy.bottomRightTap == Action::COPY_CURRENT_STRUCTURE);
    assert(policy.bottomRightHold == Action::PASTE_CURRENT_STRUCTURE);
    assert(policy.bottomRightVisibility == Visibility::ACTIVE);

    context.currentStructureCanCopy = false;
    policy = buildSequencerInteractionPolicy(context);
    assert(policy.bottomRightVisibility == Visibility::DISABLED);

    context.compatibleClipboardAvailable = true;
    policy = buildSequencerInteractionPolicy(context);
    assert(policy.bottomRightVisibility == Visibility::ACTIVE);
}

void expectsSelectorOverrides() {
    auto context = baseContext(Focus::STEP);
    context.patternQuickControlsActive = true;

    auto policy = buildSequencerInteractionPolicy(context);
    assert(policy.scope == Scope::PATTERN_DIMENSION_SELECTOR);
    assert(policy.navTurn == Action::SELECT_PATTERN_DIMENSION);
    assert(policy.navTap == Action::APPLY_PATTERN_DIMENSION_SELECTOR);
    assert(policy.optTurn == Action::EDIT_PATTERN_DIMENSION);
    assert(policy.macroLongPress == Action::NONE);
    assert(policy.leftCenterVisibility == Visibility::ACTIVE);
    assert(policy.leftBottomVisibility == Visibility::HIDDEN);

    context.patternQuickControlsActive = false;
    context.propertySelectorActive = true;
    policy = buildSequencerInteractionPolicy(context);
    assert(policy.scope == Scope::MUSICAL_PROPERTY_SELECTOR);
    assert(policy.navTurn == Action::SELECT_MUSICAL_PROPERTY);
    assert(policy.navTap == Action::APPLY_MUSICAL_PROPERTY_SELECTOR);
    assert(policy.optTurn == Action::EDIT_MUSICAL_PROPERTY_VARIATION);
    assert(policy.macroLongPress == Action::NONE);
    assert(policy.leftCenterPress == Action::APPLY_MUSICAL_PROPERTY_SELECTOR);
    assert(policy.leftBottomPress == Action::EDIT_STEP_LOCAL_RANDOM);
    assert(policy.leftCenterVisibility == Visibility::ACTIVE);
    assert(policy.leftBottomVisibility == Visibility::ACTIVE);

    context.navigationFocus = Focus::PAGE;
    policy = buildSequencerInteractionPolicy(context);
    assert(policy.scope == Scope::MUSICAL_PROPERTY_SELECTOR);
    assert(policy.leftCenterPress == Action::NONE);
    assert(policy.leftBottomPress == Action::APPLY_MUSICAL_PROPERTY_SELECTOR);
    assert(policy.leftCenterVisibility == Visibility::HIDDEN);
    assert(policy.leftBottomVisibility == Visibility::ACTIVE);
}

void expectsSelectionOverrides() {
    auto context = baseContext(Focus::STEP);
    context.stepSelectionActive = true;
    context.selectedItemsAvailable = true;
    context.compatibleClipboardAvailable = true;

    auto policy = buildSequencerInteractionPolicy(context);
    assert(policy.scope == Scope::STEP_SELECTION);
    assert(policy.navTurn == Action::MOVE_SELECTION_CURSOR);
    assert(policy.navTap == Action::TOGGLE_SELECTION);
    assert(policy.optTurn == Action::NONE);
    assert(policy.macroLongPress == Action::NONE);
    assert(policy.bottomLeftTap == Action::RESET_STEP_SELECTION_SHALLOW);
    assert(policy.bottomLeftHold == Action::RESET_STEP_SELECTION_DEEP);
    assert(policy.bottomRightTap == Action::COPY_STEP_SELECTION);
    assert(policy.bottomRightHold == Action::PASTE_STEP_SELECTION);
    assert(policy.leftCenterVisibility == Visibility::HIDDEN);
    assert(policy.leftBottomVisibility == Visibility::HIDDEN);

    context.stepSelectionActive = false;
    context.trackSelectionActive = true;
    policy = buildSequencerInteractionPolicy(context);
    assert(policy.scope == Scope::TRACK_SELECTION);
    assert(policy.bottomLeftTap == Action::MUTE_TRACK_SELECTION);
    assert(policy.bottomLeftHold == Action::DELETE_SELECTION);
    assert(policy.bottomRightTap == Action::COPY_SELECTION);
    assert(policy.bottomRightHold == Action::PASTE_SELECTION);

    context.trackSelectionActive = false;
    context.pageSelectionActive = true;
    policy = buildSequencerInteractionPolicy(context);
    assert(policy.scope == Scope::PATTERN_SELECTION);
    assert(policy.bottomLeftTap == Action::CLEAR_SELECTION);
    assert(policy.bottomLeftHold == Action::DELETE_SELECTION);
    assert(policy.bottomRightTap == Action::COPY_SELECTION);
    assert(policy.bottomRightHold == Action::PASTE_SELECTION);
}

void expectsStepEditorOverridesEverything() {
    auto context = baseContext(Focus::PAGE);
    context.stepEditorVisible = true;
    context.patternQuickControlsActive = true;
    context.propertySelectorActive = true;
    context.stepSelectionActive = true;

    auto policy = buildSequencerInteractionPolicy(context);
    assert(policy.scope == Scope::STEP_EDITOR);
    assert(policy.navTurn == Action::SELECT_STEP_EDITOR_ROW);
    assert(policy.navTap == Action::APPLY_STEP_EDITOR);
    assert(policy.optTurn == Action::EDIT_STEP_EDITOR_ROW);
    assert(policy.macroLongPress == Action::NONE);
    assert(policy.leftBottomPress == Action::EDIT_STEP_LOCAL_RANDOM);
    assert(policy.leftCenterVisibility == Visibility::HIDDEN);
    assert(policy.leftBottomVisibility == Visibility::ACTIVE);
    assert(policy.bottomLeftVisibility == Visibility::HIDDEN);
    assert(policy.bottomRightVisibility == Visibility::HIDDEN);

    context.stepEditorValueRowFocused = true;
    policy = buildSequencerInteractionPolicy(context);
    assert(policy.bottomLeftTap == Action::RESET_STEP_EDITOR_ROW);
    assert(policy.bottomLeftHold == Action::NONE);
    assert(policy.bottomRightTap == Action::NONE);
    assert(policy.bottomRightHold == Action::NONE);
    assert(policy.bottomLeftVisibility == Visibility::ACTIVE);
    assert(policy.bottomRightVisibility == Visibility::HIDDEN);

    context.stepEditorValueRowFocused = false;
    context.stepEditorContextRowFocused = true;
    context.stepEditorContextHasChild = true;
    context.compatibleClipboardAvailable = true;
    policy = buildSequencerInteractionPolicy(context);
    assert(policy.bottomLeftTap == Action::NONE);
    assert(policy.bottomLeftHold == Action::REMOVE_STEP_EDITOR_CONTEXT);
    assert(policy.bottomRightTap == Action::COPY_STEP_EDITOR_CONTEXT);
    assert(policy.bottomRightHold == Action::PASTE_STEP_EDITOR_CONTEXT);
    assert(policy.bottomLeftVisibility == Visibility::ACTIVE);
    assert(policy.bottomRightVisibility == Visibility::ACTIVE);
}

void expectsAvailabilityHelpers() {
    auto context = baseContext(Focus::PAGE);
    assert(!sequencerInteractionSelectionActive(context));
    assert(!sequencerInteractionTransientActive(context));
    assert(sequencerInteractionMainSurfaceAvailable(context));

    context.stepSelectionActive = true;
    assert(sequencerInteractionSelectionActive(context));
    assert(!sequencerInteractionMainSurfaceAvailable(context));

    context.stepSelectionActive = false;
    context.propertySelectorActive = true;
    assert(sequencerInteractionTransientActive(context));
    assert(!sequencerInteractionMainSurfaceAvailable(context));

    context.propertySelectorActive = false;
    context.historyShortcutHoldActive = true;
    assert(sequencerInteractionTransientActive(context));
    assert(!sequencerInteractionMainSurfaceAvailable(context));
}

void expectsOverlayBlocksMainSurfaceEditing() {
    auto context = baseContext(Focus::PAGE);
    context.overlayVisible = true;

    auto policy = buildSequencerInteractionPolicy(context);
    assert(policy.scope == Scope::PATTERN);
    assert(policy.navTurn == Action::MOVE_PATTERN);
    assert(policy.navTap == Action::NONE);
    assert(policy.optTurn == Action::NONE);
    assert(policy.macroLongPress == Action::NONE);
    assert(policy.leftCenterVisibility == Visibility::HIDDEN);
    assert(policy.leftBottomVisibility == Visibility::HIDDEN);
    assert(policy.bottomLeftVisibility == Visibility::HIDDEN);
    assert(policy.bottomRightVisibility == Visibility::HIDDEN);
}

void expectsDestructiveAndMuteIconsToRemainSemanticallyDistinct() {
    using core::ui::sequencer::interactionActionIcon;
    const char* mute = interactionActionIcon(Action::MUTE_CURRENT_TRACK);
    const char* clear = interactionActionIcon(Action::CLEAR_STEP_CONTENT);
    const char* reset = interactionActionIcon(Action::RESET_CURRENT_STEP_SHALLOW);
    const char* remove = interactionActionIcon(Action::REMOVE_CURRENT_STRUCTURE);

    assert(mute && clear && reset && remove);
    assert(std::strcmp(mute, clear) != 0);
    assert(std::strcmp(clear, reset) != 0);
    assert(std::strcmp(reset, remove) != 0);
    assert(std::strcmp(clear, remove) != 0);
}

}  // namespace

int main() {
    expectsRootFocusMatrix();
    expectsChildContentBottomActions();
    expectsStructureCopyVisibleWithoutClipboard();
    expectsSelectorOverrides();
    expectsSelectionOverrides();
    expectsStepEditorOverridesEverything();
    expectsAvailabilityHelpers();
    expectsOverlayBlocksMainSurfaceEditing();
    expectsDestructiveAndMuteIconsToRemainSemanticallyDistinct();

    std::cout << "SequencerInteractionPolicy tests passed\n";
    return 0;
}

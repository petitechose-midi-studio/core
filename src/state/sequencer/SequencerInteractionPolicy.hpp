#pragma once

#include <stdint.h>

#include "state/StructureSelectionState.hpp"

namespace core::state::sequencer {

enum class SequencerInteractionScope : uint8_t {
    TRACK_LEGACY,
    PATTERN,
    STEP,
    CHILD_PATTERN,
    PATTERN_DIMENSION_SELECTOR,
    MUSICAL_PROPERTY_SELECTOR,
    TRACK_SELECTION,
    PATTERN_SELECTION,
    STEP_SELECTION,
    STEP_EDITOR,
};

enum class SequencerInteractionAction : uint8_t {
    NONE,
    MOVE_TRACK,
    MOVE_PATTERN,
    MOVE_STEP,
    MOVE_SELECTION_CURSOR,
    SELECT_PATTERN_DIMENSION,
    SELECT_MUSICAL_PROPERTY,
    SELECT_STEP_EDITOR_ROW,
    CYCLE_SCOPE,
    CREATE_PREVIEW_STRUCTURE,
    ENTER_SELECTION,
    TOGGLE_SELECTION,
    OPEN_PATTERN_DIMENSION_SELECTOR,
    OPEN_MUSICAL_PROPERTY_SELECTOR,
    APPLY_PATTERN_DIMENSION_SELECTOR,
    APPLY_MUSICAL_PROPERTY_SELECTOR,
    APPLY_STEP_EDITOR,
    CANCEL_TRANSIENT_CONTEXT,
    EDIT_PATTERN_DIMENSION,
    EDIT_MUSICAL_PROPERTY_VARIATION,
    EDIT_STEP_PROPERTY,
    EDIT_STEP_LOCAL_RANDOM,
    EDIT_STEP_EDITOR_ROW,
    OPEN_STEP_EDITOR,
    TOGGLE_VISIBLE_STEP,
    EDIT_VISIBLE_STEP_PROPERTY,
    MUTE_CURRENT_TRACK,
    CLEAR_CURRENT_STRUCTURE,
    REMOVE_CURRENT_STRUCTURE,
    RESET_CURRENT_STEP_SHALLOW,
    RESET_CURRENT_STEP_DEEP,
    COPY_CURRENT_STEP,
    PASTE_CURRENT_STEP,
    CLEAR_STEP_CONTENT,
    COPY_CURRENT_STRUCTURE,
    PASTE_CURRENT_STRUCTURE,
    COPY_STEP_CONTENT,
    PASTE_STEP_CONTENT,
    RESET_STEP_EDITOR_ROW,
    REMOVE_STEP_EDITOR_CONTEXT,
    COPY_STEP_EDITOR_CONTEXT,
    PASTE_STEP_EDITOR_CONTEXT,
    MUTE_TRACK_SELECTION,
    CLEAR_SELECTION,
    DELETE_SELECTION,
    RESET_STEP_SELECTION_SHALLOW,
    RESET_STEP_SELECTION_DEEP,
    COPY_SELECTION,
    PASTE_SELECTION,
    COPY_STEP_SELECTION,
    PASTE_STEP_SELECTION,
};

enum class SequencerInteractionVisibility : uint8_t {
    HIDDEN,
    DISABLED,
    ACTIVE,
};

struct SequencerInteractionContext {
    StructureNavigationFocus navigationFocus = StructureNavigationFocus::PAGE;
    bool childContentView = false;
    bool overlayVisible = false;
    bool previewingAddSlot = false;
    bool pageSelectionActive = false;
    bool trackSelectionActive = false;
    bool stepSelectionActive = false;
    bool patternQuickControlsActive = false;
    bool propertySelectorActive = false;
    bool stepEditorVisible = false;
    bool compatibleClipboardAvailable = false;
    bool currentStructureCanClear = true;
    bool currentStructureCanRemove = true;
    bool currentStructureCanCopy = true;
    bool currentStepHasChildContent = false;
    bool selectedItemsAvailable = false;
    bool stepEditorValueRowFocused = false;
    bool stepEditorContextRowFocused = false;
    bool stepEditorContextHasChild = false;
};

struct SequencerInteractionPolicy {
    SequencerInteractionScope scope = SequencerInteractionScope::PATTERN;
    SequencerInteractionAction navTurn = SequencerInteractionAction::MOVE_PATTERN;
    SequencerInteractionAction navTap = SequencerInteractionAction::CYCLE_SCOPE;
    SequencerInteractionAction navLongPress = SequencerInteractionAction::ENTER_SELECTION;
    SequencerInteractionAction optTurn = SequencerInteractionAction::EDIT_PATTERN_DIMENSION;
    SequencerInteractionAction leftTopTap = SequencerInteractionAction::NONE;
    SequencerInteractionAction leftCenterPress = SequencerInteractionAction::OPEN_PATTERN_DIMENSION_SELECTOR;
    SequencerInteractionAction leftBottomPress = SequencerInteractionAction::OPEN_MUSICAL_PROPERTY_SELECTOR;
    SequencerInteractionAction macroTap = SequencerInteractionAction::TOGGLE_VISIBLE_STEP;
    SequencerInteractionAction macroLongPress = SequencerInteractionAction::OPEN_STEP_EDITOR;
    SequencerInteractionAction macroTurn = SequencerInteractionAction::EDIT_VISIBLE_STEP_PROPERTY;
    SequencerInteractionAction bottomLeftTap = SequencerInteractionAction::CLEAR_CURRENT_STRUCTURE;
    SequencerInteractionAction bottomLeftHold = SequencerInteractionAction::REMOVE_CURRENT_STRUCTURE;
    SequencerInteractionAction bottomRightTap = SequencerInteractionAction::COPY_CURRENT_STRUCTURE;
    SequencerInteractionAction bottomRightHold = SequencerInteractionAction::PASTE_CURRENT_STRUCTURE;
    SequencerInteractionVisibility leftCenterVisibility = SequencerInteractionVisibility::ACTIVE;
    SequencerInteractionVisibility leftBottomVisibility = SequencerInteractionVisibility::ACTIVE;
    SequencerInteractionVisibility bottomLeftVisibility = SequencerInteractionVisibility::ACTIVE;
    SequencerInteractionVisibility bottomRightVisibility = SequencerInteractionVisibility::ACTIVE;
};

bool sequencerInteractionSelectionActive(const SequencerInteractionContext& context);
bool sequencerInteractionTransientActive(const SequencerInteractionContext& context);
bool sequencerInteractionMainSurfaceAvailable(const SequencerInteractionContext& context);

SequencerInteractionPolicy buildSequencerInteractionPolicy(const SequencerInteractionContext& context);

}  // namespace core::state::sequencer

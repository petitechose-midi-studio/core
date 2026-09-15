#pragma once

#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "ui/strip/ContextActionVisualProjection.hpp"

namespace core::ui::sequencer {

inline core::state::contextual::ContextActionId interactionActionId(
    core::state::sequencer::SequencerInteractionAction action
) {
    using Id = core::state::contextual::ContextActionId;
    using Action = core::state::sequencer::SequencerInteractionAction;
    switch (action) {
        case Action::MUTE_CURRENT_TRACK:
        case Action::MUTE_TRACK_SELECTION:
            return Id::MUTE;
        case Action::CLEAR_CURRENT_STRUCTURE:
        case Action::CLEAR_STEP_CONTENT:
        case Action::CLEAR_SELECTION:
            return Id::CLEAR;
        case Action::REMOVE_CURRENT_STRUCTURE:
        case Action::REMOVE_STEP_EDITOR_CONTEXT:
        case Action::DELETE_SELECTION:
            return Id::REMOVE;
        case Action::RESET_CURRENT_STEP_SHALLOW:
        case Action::RESET_CURRENT_STEP_DEEP:
        case Action::RESET_STEP_SELECTION_SHALLOW:
        case Action::RESET_STEP_SELECTION_DEEP:
        case Action::RESET_STEP_EDITOR_ROW:
            return Id::RESET;
        case Action::COPY_CURRENT_STEP:
        case Action::COPY_CURRENT_STRUCTURE:
        case Action::COPY_STRUCTURE_SELECTION:
        case Action::COPY_STEP_CONTENT:
        case Action::COPY_STEP_SELECTION:
        case Action::COPY_STEP_EDITOR_CONTEXT:
            return Id::COPY;
        case Action::PASTE_CURRENT_STEP:
        case Action::PASTE_CURRENT_STRUCTURE:
        case Action::PASTE_STRUCTURE_SELECTION:
        case Action::PASTE_STEP_CONTENT:
        case Action::PASTE_STEP_SELECTION:
        case Action::PASTE_STEP_EDITOR_CONTEXT:
            return Id::PASTE;
        case Action::NONE:
        default:
            return Id::NONE;
    }
}

inline ContextActionStripSlotProps makeInteractionActionStripSlot(
    core::state::sequencer::SequencerInteractionAction action,
    ContextActionStripVisualState visual,
    ContextActionStripTone tone = ContextActionStripTone::NEUTRAL,
    bool holdOnly = false
) {
    return makeContextActionStripSlot(interactionActionId(action), visual, tone, holdOnly);
}

}  // namespace core::ui::sequencer

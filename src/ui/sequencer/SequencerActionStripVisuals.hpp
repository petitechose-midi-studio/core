#pragma once

#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "ui/font/StandaloneIcons.hpp"

namespace core::ui::sequencer {

inline const char* interactionActionIcon(
    core::state::sequencer::SequencerInteractionAction action
) {
    using Action = core::state::sequencer::SequencerInteractionAction;
    switch (action) {
        case Action::MUTE_CURRENT_TRACK:
        case Action::MUTE_TRACK_SELECTION:
            return standalone::icons::TRACK_MUTE;
        case Action::CLEAR_CURRENT_STRUCTURE:
        case Action::CLEAR_STEP_CONTENT:
        case Action::CLEAR_SELECTION:
            return standalone::icons::ACTION_CLEAR;
        case Action::REMOVE_CURRENT_STRUCTURE:
        case Action::REMOVE_STEP_EDITOR_CONTEXT:
        case Action::DELETE_SELECTION:
            return standalone::icons::ACTION_REMOVE;
        case Action::RESET_CURRENT_STEP_SHALLOW:
        case Action::RESET_CURRENT_STEP_DEEP:
        case Action::RESET_STEP_SELECTION_SHALLOW:
        case Action::RESET_STEP_SELECTION_DEEP:
        case Action::RESET_STEP_EDITOR_ROW:
            return standalone::icons::ACTION_RESET;
        case Action::COPY_CURRENT_STEP:
        case Action::COPY_CURRENT_STRUCTURE:
        case Action::COPY_STRUCTURE_SELECTION:
        case Action::COPY_STEP_CONTENT:
        case Action::COPY_STEP_SELECTION:
        case Action::COPY_STEP_EDITOR_CONTEXT:
            return standalone::icons::ACTION_COPY;
        case Action::PASTE_CURRENT_STEP:
        case Action::PASTE_CURRENT_STRUCTURE:
        case Action::PASTE_STRUCTURE_SELECTION:
        case Action::PASTE_STEP_CONTENT:
        case Action::PASTE_STEP_SELECTION:
        case Action::PASTE_STEP_EDITOR_CONTEXT:
            return standalone::icons::ACTION_PASTE;
        case Action::NONE:
        default:
            return nullptr;
    }
}

}  // namespace core::ui::sequencer

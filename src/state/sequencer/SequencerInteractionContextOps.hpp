#pragma once

#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::state::sequencer {

inline SequencerInteractionContext makeSequencerInteractionContext(
    const SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible = false
) {
    SequencerInteractionContext context{};
    context.navigationFocus = navigationFocus;
    context.childContentView = isChildContentView(sequencer);
    context.overlayVisible = overlayVisible;
    context.previewingAddSlot = navigationFocus == core::state::StructureNavigationFocus::TRACK
        ? trackUi.previewAddSlot.get()
        : navigationFocus == core::state::StructureNavigationFocus::PAGE
            ? sequencer.structureUi.previewAddPageSlot.get()
            : false;
    context.pageSelectionActive = sequencer.structureUi.pageSelection.active.get();
    context.trackSelectionActive = trackUi.selection.active.get();
    context.stepSelectionActive = sequencer.structureUi.stepSelection.active.get();
    context.patternQuickControlsActive = sequencer.patternQuickControls.selecting.get();
    context.propertySelectorActive = sequencer.stepPropertyInlineSelector.selecting.get();
    context.stepEditorVisible = sequencer.stepEdit.visible.get();
    return context;
}

}  // namespace core::state::sequencer

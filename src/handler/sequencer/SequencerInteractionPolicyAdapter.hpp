#pragma once

#include "state/StructureSelectionState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler::sequencer::interaction_policy {

inline core::state::sequencer::SequencerInteractionContext makeContext(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible = false
) {
    core::state::sequencer::SequencerInteractionContext context{};
    context.navigationFocus = navigationFocus;
    context.childContentView = core::state::sequencer::isChildContentView(sequencer);
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

inline core::state::sequencer::SequencerInteractionPolicy build(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible = false
) {
    return core::state::sequencer::buildSequencerInteractionPolicy(
        makeContext(sequencer, trackUi, navigationFocus, overlayVisible)
    );
}

}  // namespace core::handler::sequencer::interaction_policy

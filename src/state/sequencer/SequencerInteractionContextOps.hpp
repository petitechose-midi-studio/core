#pragma once

#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerState.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

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
    if (context.stepEditorVisible) {
        const uint8_t row = sequencer.stepEdit.focusedRow.get();
        context.stepEditorValueRowFocused =
            step_edit_rows::isActivated(row) ||
            step_edit_rows::isProperty(row) ||
            step_edit_rows::isChord(row);
        context.stepEditorContextRowFocused = step_edit_rows::isContext(row);
        if (context.stepEditorContextRowFocused) {
            const auto projection = resolveActiveContentStepProjection(
                sequencer,
                sequencer.stepEdit.stepIndex.get(),
                {}
            );
            context.stepEditorContextHasChild =
                projection.valid &&
                stepContentProjectionHasChild(
                    projection,
                    step_edit_rows::childKindForContextRow(row)
                );
        }
    }
    return context;
}

}  // namespace core::state::sequencer

#include "state/sequencer/SequencerInteractionContextOps.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/SequencerStepEditRows.hpp"

namespace core::state::sequencer {

FLASHMEM SequencerInteractionContext makeSequencerInteractionContext(
    const SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible
) {
    SequencerInteractionContext context{};
    context.navigationFocus = navigationFocus;
    context.childContentView = isChildContentView(sequencer);
    const bool drumTransientVisible =
        sequencer.drumSequencer.active() &&
        (!sequencer.drumSequencer.gridVisible() ||
         sequencer.drumSequencer.selectorVisible());
    context.overlayVisible = overlayVisible || sequencer.ccLaneUi.visible() ||
                             sequencer.contextSelector.visible ||
                             drumTransientVisible ||
                             sequencer.stepContentDraft.exitPromptVisible.get() ||
                             sequencer.patternPresetPreview.active() ||
                             sequencer.patternEditor.active.get();
    context.previewingAddSlot =
        context.navigationFocus == core::state::StructureNavigationFocus::TRACK
            ? trackUi.previewAddSlot.get()
            : false;
    context.trackSelectionActive = trackUi.selection.active.get();
    context.pageSelectionActive = sequencer.structureUi.pageSelection.active.get();
    context.stepSelectionActive = sequencer.structureUi.stepSelection.active.get();
    context.drumLaneSelectionActive =
        sequencer.drumSequencer.laneSelection.active;
    if (context.trackSelectionActive) {
        context.selectionPlacementActive =
            trackUi.selection.placementActive();
        context.selectedItemsAvailable =
            trackUi.selection.anySelected();
    } else if (context.pageSelectionActive) {
        context.selectionPlacementActive =
            sequencer.structureUi.pageSelection.placementActive();
        context.selectedItemsAvailable =
            sequencer.structureUi.pageSelection.anySelected();
    } else if (context.stepSelectionActive) {
        context.selectionPlacementActive =
            sequencer.structureUi.stepSelection.placementActive();
        context.selectedItemsAvailable =
            sequencer.structureUi.stepSelection.anySelected();
    } else if (context.drumLaneSelectionActive) {
        context.selectionPlacementActive =
            sequencer.drumSequencer.laneSelection.placementActive();
        context.selectedItemsAvailable =
            sequencer.drumSequencer.laneSelection.anySelected();
    }
    context.patternQuickControlsActive = sequencer.patternQuickControls.selecting.get();
    context.propertySelectorActive = sequencer.stepPropertyInlineSelector.selecting.get();
    context.stepContentSelectorActive = sequencer.stepContentSelector.selecting.get();
    context.stepEditorVisible = sequencer.stepEdit.visible.get();
    if (context.stepEditorVisible) {
        context.stepEditorDrumRoot = sequencer.stepEdit.drumContext &&
            isRootContentView(sequencer);
        if (context.stepEditorDrumRoot) {
            uint8_t adjacentLane = 0U;
            context.stepEditorLaneRetargetAvailable =
                sequencer.drumSequencer.adjacentLaneForStep(
                    sequencer.stepEdit.drumLane,
                    sequencer.stepEdit.drumStep,
                    1,
                    adjacentLane
                );
        }
        const uint8_t row = sequencer.stepEdit.focusedRow.get();
        context.stepEditorValueRowFocused =
            step_edit_rows::isActivated(row) ||
            step_edit_rows::isProperty(row) ||
            step_edit_rows::isChord(row);
        context.stepEditorContextRowFocused = step_edit_rows::isContext(row);
        if (context.stepEditorContextRowFocused) {
            const auto nodeId = activeContentStepNodeId(
                sequencer,
                sequencer.stepEdit.stepIndex.get()
            );
            const auto childKind = step_edit_rows::childKindForContextRow(row);
            context.stepEditorContextHasChild =
                childKind == StepContentChildKind::MICRO_SEQUENCE
                    ? stepNodeHasMicroSequence(authoringPattern(sequencer), nodeId)
                    : stepNodeHasCycleStateSet(authoringPattern(sequencer), nodeId);
        }
    }
    return context;
}

}  // namespace core::state::sequencer

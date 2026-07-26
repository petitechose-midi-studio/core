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
    context.overlayVisible = overlayVisible || sequencer.ccLaneUi.visible() ||
                             sequencer.stepContentDraft.exitPromptVisible.get() ||
                             sequencer.patternEditor.active.get();
    context.previewingAddSlot =
        context.navigationFocus == core::state::StructureNavigationFocus::TRACK
        ? trackUi.previewAddSlot.get()
        : context.navigationFocus == core::state::StructureNavigationFocus::PAGE
            ? sequencer.structureUi.previewAddPageSlot.get()
            : false;
    context.trackSelectionActive = trackUi.selection.active.get();
    context.pageSelectionActive = sequencer.structureUi.pageSelection.active.get();
    context.stepSelectionActive = sequencer.structureUi.stepSelection.active.get();
    context.patternQuickControlsActive = sequencer.patternQuickControls.selecting.get();
    context.propertySelectorActive = sequencer.stepPropertyInlineSelector.selecting.get();
    context.stepContentSelectorActive = sequencer.stepContentSelector.selecting.get();
    context.stepEditorVisible = sequencer.stepEdit.visible.get();
    if (context.stepEditorVisible) {
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

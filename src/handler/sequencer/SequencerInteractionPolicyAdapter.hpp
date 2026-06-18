#pragma once

#include "state/StructureSelectionState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler::sequencer::interaction_policy {

using Action = core::state::sequencer::SequencerInteractionAction;
using Policy = core::state::sequencer::SequencerInteractionPolicy;

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

inline bool allowsMainSurface(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible = false
) {
    return core::state::sequencer::sequencerInteractionMainSurfaceAvailable(
        makeContext(sequencer, trackUi, navigationFocus, overlayVisible)
    );
}

inline bool selectionActive(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible = false
) {
    return core::state::sequencer::sequencerInteractionSelectionActive(
        makeContext(sequencer, trackUi, navigationFocus, overlayVisible)
    );
}

inline bool canOpenPatternDimensionSelector(const Policy& policy) {
    return policy.leftCenterPress == Action::OPEN_PATTERN_DIMENSION_SELECTOR;
}

inline bool canOpenMusicalPropertySelector(const Policy& policy) {
    return policy.leftBottomPress == Action::OPEN_MUSICAL_PROPERTY_SELECTOR;
}

inline bool canOpenStepEditor(const Policy& policy) {
    return policy.macroLongPress == Action::OPEN_STEP_EDITOR;
}

inline bool canOptEditPatternDimension(const Policy& policy) {
    return policy.optTurn == Action::EDIT_PATTERN_DIMENSION;
}

inline bool canEditStepProperty(const Policy& policy) {
    return policy.optTurn == Action::EDIT_STEP_PROPERTY;
}

inline bool canEditVisibleStepProperty(const Policy& policy) {
    return policy.macroTurn == Action::EDIT_VISIBLE_STEP_PROPERTY;
}

inline bool canEditMusicalPropertyVariation(const Policy& policy) {
    return policy.macroTurn == Action::EDIT_MUSICAL_PROPERTY_VARIATION;
}

}  // namespace core::handler::sequencer::interaction_policy

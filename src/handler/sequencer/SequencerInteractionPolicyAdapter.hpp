#pragma once

#include "state/sequencer/SequencerInteractionContextOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"

namespace core::handler::sequencer::interaction_policy {

using Action = core::state::sequencer::SequencerInteractionAction;
using Policy = core::state::sequencer::SequencerInteractionPolicy;

inline core::state::sequencer::SequencerInteractionContext makeContext(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible = false
) {
    return core::state::sequencer::makeSequencerInteractionContext(
        sequencer,
        trackUi,
        navigationFocus,
        overlayVisible
    );
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
    return policy.leftCenterPress == Action::OPEN_MUSICAL_PROPERTY_SELECTOR ||
           policy.leftBottomPress == Action::OPEN_MUSICAL_PROPERTY_SELECTOR;
}

inline bool canOpenMusicalPropertySelectorFromLeftCenter(const Policy& policy) {
    return policy.leftCenterPress == Action::OPEN_MUSICAL_PROPERTY_SELECTOR;
}

inline bool canOpenMusicalPropertySelectorFromLeftBottom(const Policy& policy) {
    return policy.leftBottomPress == Action::OPEN_MUSICAL_PROPERTY_SELECTOR;
}

inline bool canApplyMusicalPropertySelectorFromLeftCenter(const Policy& policy) {
    return policy.leftCenterPress == Action::APPLY_MUSICAL_PROPERTY_SELECTOR;
}

inline bool canApplyMusicalPropertySelectorFromLeftBottom(const Policy& policy) {
    return policy.leftBottomPress == Action::APPLY_MUSICAL_PROPERTY_SELECTOR;
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

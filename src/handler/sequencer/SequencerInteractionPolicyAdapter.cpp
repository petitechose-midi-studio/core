#include "handler/sequencer/SequencerInteractionPolicyAdapter.hpp"

#include <config/PlatformCompat.hpp>

namespace core::handler::sequencer::interaction_policy {

FLASHMEM core::state::sequencer::SequencerInteractionContext makeContext(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible
) {
    return core::state::sequencer::makeSequencerInteractionContext(
        sequencer,
        trackUi,
        navigationFocus,
        overlayVisible
    );
}

FLASHMEM core::state::sequencer::SequencerInteractionPolicy build(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible
) {
    return core::state::sequencer::buildSequencerInteractionPolicy(
        makeContext(sequencer, trackUi, navigationFocus, overlayVisible)
    );
}

FLASHMEM bool allowsMainSurface(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible
) {
    return core::state::sequencer::sequencerInteractionMainSurfaceAvailable(
        makeContext(sequencer, trackUi, navigationFocus, overlayVisible)
    );
}

FLASHMEM bool selectionActive(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::TrackNavigationState& trackUi,
    core::state::StructureNavigationFocus navigationFocus,
    bool overlayVisible
) {
    return core::state::sequencer::sequencerInteractionSelectionActive(
        makeContext(sequencer, trackUi, navigationFocus, overlayVisible)
    );
}

}  // namespace core::handler::sequencer::interaction_policy

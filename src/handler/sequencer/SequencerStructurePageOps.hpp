#pragma once

#include "state/sequencer/SequencerSnapshotOps.hpp"
#include "state/sequencer/SequencerState.hpp"

namespace core::handler {

inline uint8_t sequencerStructurePageTarget(
    const core::state::sequencer::SequencerState& sequencer
) {
    return sequencer.structureUi.previewAddPageSlot.get()
        ? sequencer.clampPage(sequencer.structureUi.previewPageIndex.get())
        : sequencer.activePageCount();
}

inline bool createSequencerStructurePage(
    core::state::sequencer::SequencerState& sequencer
) {
    return core::state::sequencer::ensurePageExists(
        sequencer,
        sequencerStructurePageTarget(sequencer)
    );
}

inline void syncSequencerPagePreviewToVisible(
    core::state::sequencer::SequencerState& sequencer,
    bool syncFocusedStep
) {
    sequencer.structureUi.previewAddPageSlot.set(false);
    sequencer.structureUi.syncPreviewPage(sequencer.visiblePage());
    if (syncFocusedStep) {
        sequencer.focusedStep.set(sequencer.pageStartStep(sequencer.visiblePage()));
    }
}

}  // namespace core::handler

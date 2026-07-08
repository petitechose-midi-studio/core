#include "handler/sequencer/SequencerStructurePageOps.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerSnapshotOps.hpp"

namespace core::handler {

FLASHMEM uint8_t sequencerStructurePageTarget(
    const core::state::sequencer::SequencerState& sequencer
) {
    return sequencer.structureUi.previewAddPageSlot.get()
        ? sequencer.clampPage(sequencer.structureUi.previewPageIndex.get())
        : sequencer.activePageCount();
}

FLASHMEM bool createSequencerStructurePage(
    core::state::sequencer::SequencerState& sequencer
) {
    return core::state::sequencer::ensurePageExists(
        sequencer,
        sequencerStructurePageTarget(sequencer)
    );
}

FLASHMEM void syncSequencerPagePreviewToVisible(
    core::state::sequencer::SequencerState& sequencer,
    bool syncFocusedStep
) {
    sequencer.structureUi.previewAddPageSlot.set(false);
    sequencer.structureUi.syncPreviewPage(sequencer.visiblePage());
    if (syncFocusedStep) {
        sequencer.focusedStep.set(sequencer.pageStartStep(sequencer.visiblePage()));
    }
}

FLASHMEM bool clearCurrentSequencerStructurePage(
    core::state::sequencer::SequencerState& sequencer
) {
    if (sequencer.structureUi.previewAddPageSlot.get()) return false;

    const uint8_t start = sequencer.pageStartStepClamped(sequencer.visiblePage());
    const uint8_t end = static_cast<uint8_t>(std::min<uint16_t>(
        core::state::sequencer::SequencerState::MAX_STEPS - 1,
        static_cast<uint16_t>(
            start + core::state::sequencer::SequencerState::STEPS_PER_PAGE - 1
        )
    ));
    return core::state::sequencer::clearStepRange(sequencer, start, end);
}

FLASHMEM bool removeCurrentSequencerStructurePage(
    core::state::sequencer::SequencerState& sequencer
) {
    if (sequencer.structureUi.previewAddPageSlot.get()) return false;
    return core::state::sequencer::removePage(sequencer, sequencer.visiblePage());
}

}  // namespace core::handler

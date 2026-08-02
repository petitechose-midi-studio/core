#include "handler/sequencer/SequencerStructurePageOps.hpp"

#include <config/PlatformCompat.hpp>

namespace core::handler {

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

}  // namespace core::handler

#include "handler/sequencer/SequencerStructureStepPasteWorkflow.hpp"

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::handler {

FLASHMEM core::state::project::ProjectStepPasteMode structureStepPasteMode(
    const core::state::project::ProjectNavigationState& projectNavigation
) {
    return core::state::project::sanitizeProjectStepPasteMode(
        projectNavigation.stepPasteMode
    );
}

FLASHMEM core::state::sequencer::SequencerStepPastePreviewPlan buildStructureStepPastePlan(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::SequencerStepsClipboard& clipboard,
    core::state::project::ProjectStepPasteMode mode,
    uint8_t cursorStep
) {
    return core::state::sequencer::buildStepPastePreviewPlan(
        clipboard,
        core::state::sequencer::isRootContentView(sequencer),
        cursorStep,
        core::state::sequencer::activeContentLength(sequencer),
        core::state::sequencer::maxStepCursorForPaste(sequencer),
        mode
    );
}

FLASHMEM void beginStructureStepPastePreview(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    const core::state::project::ProjectNavigationState& projectNavigation
) {
    auto& selection = sequencer.structureUi.stepSelection;
    if (!selection.placementActive() ||
        selection.clipboardRevision.get() !=
            structureClipboard.revision.get()) {
        return;
    }

    selection.pastePreviewActive.set(true);
    if (!structureClipboard.hasSequencerSteps()) {
        selection.pastePreview.set(core::state::sequencer::SequencerStepPastePreview::BLOCKED);
        return;
    }

    const auto plan = buildStructureStepPastePlan(
        sequencer,
        structureClipboard.sequencerSteps,
        structureStepPasteMode(projectNavigation),
        selection.cursorStep.get()
    );
    selection.pastePreview.set(plan.aggregate);
}

FLASHMEM void clearStructureStepPastePreview(
    core::state::sequencer::SequencerState& sequencer
) {
    auto& selection = sequencer.structureUi.stepSelection;
    selection.pastePreviewActive.set(false);
    selection.pastePreview.set(core::state::sequencer::SequencerStepPastePreview::NONE);
}

}  // namespace core::handler

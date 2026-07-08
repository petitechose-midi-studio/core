#include "handler/sequencer/SequencerStructureStepPasteWorkflow.hpp"

#include <config/PlatformCompat.hpp>

#include "handler/sequencer/SequencerStructureStepOps.hpp"
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
    if (!selection.active.get()) return;

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

FLASHMEM bool commitStructureStepPastePlan(
    core::state::sequencer::SequencerState& sequencer,
    const core::state::StructureClipboardState& structureClipboard,
    core::state::project::ProjectStepPasteMode mode,
    const core::state::sequencer::SequencerStepPastePreviewPlan& plan
) {
    if (!structureClipboard.hasSequencerSteps()) return false;
    if (plan.blocked || !plan.hasEntries()) return false;

    if (!core::state::sequencer::resizeActiveContentForStepPaste(
            sequencer,
            mode,
            plan.lastTarget,
            core::state::sequencer::maxStepCursorForPaste(sequencer)
        )) {
        return false;
    }

    const auto* sourceGraph = structureClipboard.sequencerGraph.get();
    bool changed = false;
    for (uint8_t i = 0; i < plan.count; ++i) {
        const auto& preview = plan.entries[i];
        if (!preview.valid) continue;
        const auto& entry =
            structureClipboard.sequencerSteps.entries[preview.clipboardIndex];
        if (!entry.valid) continue;
        changed = structureClipboard.sequencerSteps.rootContext
            ? writeRootStepFromClipboardEntry(
                  sequencer,
                  entry,
                  sourceGraph,
                  preview.targetStep
              ) || changed
            : writeChildStepFromClipboardEntry(
                  sequencer,
                  entry,
                  sourceGraph,
                  preview.targetStep
              ) || changed;
    }
    return changed;
}

}  // namespace core::handler

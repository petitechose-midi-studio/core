#include "ui/sequencer/SequencerStepGridViewModelBuilder.hpp"

#include <cstdint>

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepPastePlan.hpp"
#include "ui/sequencer/StepGridFrameLogic.hpp"

namespace core::ui::sequencer {

namespace {

FLASHMEM void applyStepPasteFootprint(
    grid::StepGridFrameState& frame,
    const SequencerViewModelSource& source
) {
    const auto& selection = source.sequencer.structureUi.stepSelection;
    if (!selection.active.get() || !source.structureClipboard.hasSequencerSteps()) return;

    const auto mode = core::state::project::sanitizeProjectStepPasteMode(
        source.projectNavigation.stepPasteMode
    );
    const uint8_t activeLength = core::state::sequencer::activeContentLength(source.sequencer);
    const uint8_t maxStep = core::state::sequencer::maxStepCursorForPaste(source.sequencer);
    const auto plan = core::state::sequencer::buildStepPastePreviewPlan(
        source.structureClipboard.sequencerSteps,
        core::state::sequencer::isRootContentView(source.sequencer),
        selection.cursorStep.get(),
        activeLength,
        maxStep,
        mode
    );

    for (uint8_t i = 0; i < plan.count; ++i) {
        const auto& entry = plan.entries[i];
        if (!entry.valid) continue;
        for (auto& tile : frame.tiles) {
            if (tile.absoluteStep != entry.targetStep) continue;
            tile.stepPastePreviewActive = true;
            tile.stepPastePreview = entry.preview;
            break;
        }
    }

    if (!plan.blocked) return;
    for (auto& tile : frame.tiles) {
        if (!tile.stepSelectionCursor) continue;
        tile.stepPastePreviewActive = true;
        tile.stepPastePreview = core::state::sequencer::SequencerStepPastePreview::BLOCKED;
        return;
    }
}

}  // namespace

FLASHMEM grid::StepGridFrameState buildSequencerStepGridProps(
    const SequencerViewModelSource& source
) {
    auto frame = grid::buildStepGridFrameState(
        source.sequencer,
        source.tracks.projectScaleSettings(),
        source.navigationFocus.get() == core::state::StructureNavigationFocus::STEP
    );
    applyStepPasteFootprint(frame, source);
    return frame;
}

}  // namespace core::ui::sequencer

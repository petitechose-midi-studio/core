#include "ui/sequencer/SequencerStepGridViewModelBuilder.hpp"

#include <cstdint>

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerStepPastePlan.hpp"
#include "ui/sequencer/StepGridFrameLogic.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui::sequencer {

namespace {

FLASHMEM void applyStepPasteFootprint(
    grid::StepGridFrameState& frame,
    const SequencerViewModelSource& source
) {
    const auto& selection = source.sequencer.structureUi.stepSelection;
    if (!selection.placementActive() ||
        selection.clipboardRevision.get() !=
            source.structureClipboard.revision.get() ||
        !source.structureClipboard.hasSequencerSteps()) {
        return;
    }

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
    frame.accentColor = standalone::theme::color::trackColor(
        source.sharedTrackActive.get()
    );
    if (core::state::sequencer::isDrumContentView(source.sequencer)) {
        const auto& content = source.sequencer.contentView;
        const auto* drumTrack = source.sequencer.drumSequencer.drumTrack;
        if (drumTrack != nullptr &&
            content.drumOwnerLane < drumTrack->kit.laneCount &&
            content.drumOwnerLane < core::state::sequencer::DRUM_MAX_LANES) {
            frame.presentation = grid::StepGridPresentation::DRUM_LANE;
            frame.accentColor = standalone::theme::color::trackColor(
                core::state::sequencer::drumLaneDisplayColorIndex(
                    drumTrack->kit.lanes[content.drumOwnerLane]
                )
            );
        }
    }
    applyStepPasteFootprint(frame, source);
    return frame;
}

}  // namespace core::ui::sequencer

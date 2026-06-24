#include "ui/sequencer/StepGridFrameLogic.hpp"

#include <algorithm>

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "ui/sequencer/StepContentBadgeProjection.hpp"

namespace core::ui::sequencer::grid {

namespace {

FLASHMEM uint8_t maxStepCursorForSelection(
    const core::state::sequencer::SequencerState& sequencer
) {
    if (core::state::sequencer::isMicroSequenceContentView(sequencer)) {
        return static_cast<uint8_t>(
            oc::note::sequencer::StepSequencerGraphLimits::MAX_EXPANDED_NOTES_PER_ROOT_STEP - 1U
        );
    }
    if (core::state::sequencer::isCycleStatesContentView(sequencer)) {
        return static_cast<uint8_t>(
            oc::note::sequencer::StepSequencerGraphLimits::MAX_CYCLE_STATES_PER_SET - 1U
        );
    }
    return static_cast<uint8_t>(core::state::sequencer::SequencerState::MAX_STEPS - 1U);
}

FLASHMEM void applyResolvedStepToTile(
    const core::state::sequencer::SequencerResolvedStepDisplayState& resolved,
    TileRenderState& tile
) {
    tile.inPattern = resolved.inPattern;
    tile.enabled = resolved.enabled;
    tile.playheadVisible = resolved.playheadVisible;
    tile.playing = resolved.playing;
    tile.probabilityCycleActive = resolved.probabilityCycleActive;
    tile.note = resolved.note;
    tile.velocity = resolved.velocity;
    tile.probability = resolved.probability;
    tile.gate = resolved.gate;
    tile.nudge = resolved.nudge;
    tile.childContentContext = resolved.childContentContext;
    tile.childContentOffset = resolved.childContentOffset;
    tile.childContentNoteOffsetUsesScaleDegrees =
        resolved.childContentNoteOffsetUsesScaleDegrees;
    tile.childPitchSummaryVisible = resolved.childPitchSummaryVisible;
    tile.childPitchSummaryNote = resolved.childPitchSummaryNote;
    tile.variation.visible = resolved.variation.visible;
    tile.variation.rangeVisible = resolved.variation.rangeVisible;
    tile.variation.deltaVisible = resolved.variation.deltaVisible;
    tile.variation.rangeProperty = resolved.variation.rangeProperty;
    tile.variation.resolved = resolved.variation.resolved;
}

}  // namespace

FLASHMEM StepGridFrameState buildStepGridFrameState(
    const core::state::sequencer::SequencerState& sequencer,
    oc::note::sequencer::StepSequencerScaleSettings projectScaleSettings,
    bool stepFocusActive
) {
    StepGridFrameState frame;
    frame.activeProperty = sequencer.activeStepProperty.get();
    frame.feedbackVisible = sequencer.stepInlineFeedback.visible.get();
    frame.feedbackTouchedMask = sequencer.stepInlineFeedback.touchedMask.get();
    frame.feedbackProperty = sequencer.stepInlineFeedback.property.get();

    const auto context =
        core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
            sequencer,
            projectScaleSettings,
            frame.activeProperty
        );
    const uint8_t length = context.length;
    const auto& stepSelection = sequencer.structureUi.stepSelection;
    const bool stepSelectionActive = stepSelection.active.get();
    const bool stepCursorVisible = stepSelectionActive || stepFocusActive;
    const uint8_t page = stepSelectionActive
        ? static_cast<uint8_t>(
              std::min<uint16_t>(
                  sequencer.page.get(),
                  core::state::sequencer::activeContentPageForStep(
                      maxStepCursorForSelection(sequencer)
                  )
              )
          )
        : static_cast<uint8_t>(
              std::min<uint16_t>(
                  sequencer.page.get(),
                  static_cast<uint16_t>(
                      core::state::sequencer::SequencerState::PAGE_COUNT - 1U
                  )
              )
          );
    const uint8_t pageStart = static_cast<uint8_t>(
        page * core::state::sequencer::SequencerState::STEPS_PER_PAGE
    );

    for (uint8_t i = 0; i < frame.tiles.size(); ++i) {
        const uint8_t absoluteStep = static_cast<uint8_t>(pageStart + i);
        auto& tile = frame.tiles[i];
        tile.absoluteStep = absoluteStep;
        tile.inPattern = absoluteStep < length;
        tile.stepSelectionActive = stepCursorVisible;
        tile.stepSelectionCursor =
            (stepSelectionActive
                 ? stepSelection.cursorStep.get()
                 : sequencer.focusedStep.get()) == absoluteStep &&
            stepCursorVisible;
        tile.stepSelectionSelected =
            stepSelectionActive && stepSelection.selectedMask.get().test(absoluteStep);
        tile.stepPastePreviewActive =
            stepSelectionActive &&
            stepSelection.pastePreviewActive.get() &&
            stepSelection.cursorStep.get() == absoluteStep;
        tile.stepPastePreview = tile.stepPastePreviewActive
            ? stepSelection.pastePreview.get()
            : core::state::sequencer::SequencerStepPastePreview::NONE;

        if (!tile.inPattern) {
            continue;
        }

        const bool stepInlineEditActive =
            frame.feedbackVisible && frame.feedbackTouchedMask.test(absoluteStep);
        const auto resolved =
            core::state::sequencer::buildSequencerResolvedStepDisplayState(
                context,
                absoluteStep,
                stepInlineEditActive
            );
        if (!resolved.valid) {
            tile.inPattern = false;
            tile.enabled = false;
            continue;
        }

        applyResolvedStepToTile(resolved, tile);
        tile.contentBadges = buildStepContentBadgeProjectionForNode(
            sequencer.pattern,
            resolved.nodeId
        );
        mergeExpandedTelemetryChordBadgeForNode(
            tile.contentBadges,
            sequencer.expandedVariationTelemetry,
            resolved.runtimeNodeId,
            sequencer.playheadStep.get(),
            sequencer.playheadStepTickOffset.get()
        );
    }

    return frame;
}

}  // namespace core::ui::sequencer::grid

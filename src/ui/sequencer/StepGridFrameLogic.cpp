#include "ui/sequencer/StepGridFrameLogic.hpp"

#include <algorithm>
#include <limits>

#include <config/PlatformCompat.hpp>
#include <oc/note/clock/ClockConstants.hpp>
#include <oc/note/sequencer/StepSequencerExpander.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "ui/sequencer/StepContentBadgeProjection.hpp"
#include "ui/sequencer/StepGridRenderLogic.hpp"

namespace core::ui::sequencer::grid {

namespace {

using ExpandedTelemetry =
    oc::note::sequencer::StepSequencerExpandedVariationTelemetry;
using ResolvedVariation =
    oc::note::sequencer::StepSequencerResolvedVariation;

FLASHMEM uint8_t ticksPerStep(
    const core::state::sequencer::SequencerPatternState& pattern
) {
    const uint8_t stepsPerBeat = std::max<uint8_t>(
        1U,
        pattern.stepsPerBeat.get()
    );
    return std::max<uint8_t>(
        1U,
        static_cast<uint8_t>(oc::note::clock::PPQN / stepsPerBeat)
    );
}

FLASHMEM void appendProjectedNote(
    TileNoteEventProjection& projection,
    uint32_t localTick,
    uint16_t sourceSpanTicks,
    const ResolvedVariation& variation,
    uint8_t rootTicksPerStep,
    bool active
) {
    if (projection.count >= projection.events.size()) {
        projection.dense = true;
        return;
    }

    const uint32_t safeRootSpan = std::max<uint8_t>(1U, rootTicksPerStep);
    const uint32_t childSpanQ8 = std::max<uint32_t>(
        1U,
        (static_cast<uint64_t>(std::max<uint16_t>(1U, sourceSpanTicks)) *
         256U + safeRootSpan / 2U) /
            safeRootSpan
    );
    const int32_t nudgeQ8 = static_cast<int32_t>(
        (static_cast<int64_t>(variation.resolved.nudge) * childSpanQ8) /
        100
    );
    const int64_t startQ8 = static_cast<int64_t>(
        (static_cast<uint64_t>(localTick) * 256U + safeRootSpan / 2U) /
        safeRootSpan
    ) + nudgeQ8;
    const uint64_t gateSpanQ8 = std::max<uint64_t>(
        1U,
        (static_cast<uint64_t>(childSpanQ8) *
         variation.resolved.gate) /
            100U
    );

    auto& event = projection.events[projection.count++];
    event.startQ8 = static_cast<int16_t>(std::clamp<int64_t>(
        startQ8,
        std::numeric_limits<int16_t>::min(),
        std::numeric_limits<int16_t>::max()
    ));
    event.spanQ8 = static_cast<uint16_t>(std::min<uint64_t>(
        gateSpanQ8,
        std::numeric_limits<uint16_t>::max()
    ));
    event.note = variation.resolved.note;
    event.velocity = variation.resolved.velocity;
    event.active = static_cast<uint8_t>(
        active && variation.resolved.gate > 0U
    );
}

FLASHMEM ResolvedVariation fallbackVariation(const TileRenderState& tile) {
    ResolvedVariation variation{};
    variation.triggered = tile.probabilityCycleActive;
    variation.base = {
        .note = tile.note,
        .velocity = tile.velocity,
        .gate = tile.gate,
        .nudge = tile.nudge,
    };
    variation.resolved = variation.base;
    if (tile.variation.visible && tile.variation.deltaVisible) {
        variation = tile.variation.resolved;
    }
    return variation;
}

FLASHMEM uint8_t sourceRootIndex(
    const oc::note::sequencer::StepSequencerGraph& graph,
    uint8_t logicalStep
) {
    const auto* root = graph.sequence(graph.rootSequenceId);
    if (root == nullptr || root->length == 0U) return logicalStep;
    int source = static_cast<int>(logicalStep) -
                 static_cast<int>(root->offset);
    source %= static_cast<int>(root->length);
    if (source < 0) source += root->length;
    return static_cast<uint8_t>(source);
}

FLASHMEM void projectExactTelemetry(
    TileNoteEventProjection& projection,
    const ExpandedTelemetry& telemetry,
    uint8_t rootTicksPerStep,
    bool active
) {
    const uint8_t requestedCount = telemetry.requestedNoteCount > 0U
        ? telemetry.requestedNoteCount
        : telemetry.count;
    projection.dense = telemetry.noteBudgetExceeded ||
        requestedCount > projection.events.size();
    for (uint8_t index = 0U; index < telemetry.count; ++index) {
        appendProjectedNote(
            projection,
            telemetry.localTick[index],
            telemetry.spanTicks[index],
            telemetry.variation[index],
            rootTicksPerStep,
            active
        );
    }
}

FLASHMEM void projectExpandedPreview(
    TileNoteEventProjection& projection,
    const oc::note::sequencer::StepSequencerExpansion& expansion,
    uint8_t rootTicksPerStep,
    bool active
) {
    const uint8_t requestedCount = expansion.requestedNoteCount > 0U
        ? expansion.requestedNoteCount
        : expansion.count;
    projection.dense = expansion.noteBudgetExceeded ||
        expansion.depthLimitReached ||
        requestedCount > projection.events.size();
    for (uint8_t index = 0U; index < expansion.count; ++index) {
        const auto& note = expansion.notes[index];
        appendProjectedNote(
            projection,
            note.localTick,
            note.spanTicks,
            note.variation,
            rootTicksPerStep,
            active
        );
    }
}

FLASHMEM void populateTileNoteEvents(
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerPatternState& pattern,
    oc::note::sequencer::StepSequencerScaleSettings scaleSettings,
    bool childContext,
    uint8_t absoluteStep,
    bool inlineEditActive,
    TileRenderState& tile
) {
    tile.noteEvents = {};
    if (!tile.inPattern || !tile.enabled) return;

    const uint8_t rootTicksPerStep = ticksPerStep(pattern);
    if (inlineEditActive) {
        ResolvedVariation edited{};
        edited.triggered = tile.probabilityCycleActive;
        edited.base = {
            .note = tile.note,
            .velocity = tile.velocity,
            .gate = tile.gate,
            .nudge = tile.nudge,
        };
        edited.resolved = edited.base;
        appendProjectedNote(
            tile.noteEvents,
            0U,
            rootTicksPerStep,
            edited,
            rootTicksPerStep,
            tile.probabilityCycleActive
        );
        return;
    }
    if (!childContext &&
        sequencer.expandedVariationTelemetry.valid &&
        sequencer.expandedVariationTelemetry.rootStepIndex == absoluteStep) {
        projectExactTelemetry(
            tile.noteEvents,
            sequencer.expandedVariationTelemetry,
            rootTicksPerStep,
            tile.probabilityCycleActive
        );
        if (tile.noteEvents.count > 0U) return;
    }

    bool graphPreviewAttempted = false;
    const auto* graph = core::state::sequencer::graphView(pattern);
    const bool needsExpandedPreview =
        tile.contentBadges.microSequence ||
        tile.contentBadges.cycleStates ||
        tile.contentBadges.chord;
    if (!childContext && graph != nullptr && graph->enabled &&
        graph->sequence(graph->rootSequenceId) != nullptr &&
        needsExpandedPreview) {
        graphPreviewAttempted = true;
        const uint8_t sourceStep = sourceRootIndex(*graph, absoluteStep);
        if (sourceStep < pattern.note.size()) {
            const auto expansion =
                oc::note::sequencer::StepSequencerExpander::expandRootStep(
                    oc::note::sequencer::StepSequencerRootStepInput{
                        .enabled = true,
                        .values = {
                            .note = pattern.note[sourceStep],
                            .velocity = pattern.velocity[sourceStep],
                            .gate = pattern.gate[sourceStep],
                            .nudge = pattern.nudge[sourceStep],
                        },
                        // Root probability authority is the published cycle
                        // mask. A structural preview must not invent a second
                        // independent root decision.
                        .probability = 100U,
                        .variationRanges = {},
                        .scaleSettings = scaleSettings,
                        .pitchFollowsScale =
                            pattern.pitchEditMode ==
                            core::state::sequencer::SequencerPitchEditMode::FOLLOW_SCALE,
                        .mode = oc::note::sequencer::
                            StepSequencerRootStepInput::Mode::Full,
                    },
                    *graph,
                    absoluteStep,
                    sequencer.probabilityCycleIndex,
                    rootTicksPerStep,
                    0U,
                    true
                );
            projectExpandedPreview(
                tile.noteEvents,
                expansion,
                rootTicksPerStep,
                tile.probabilityCycleActive
            );
            if (tile.noteEvents.count > 0U) return;
        }
    }

    const ResolvedVariation fallback = fallbackVariation(tile);
    appendProjectedNote(
        tile.noteEvents,
        0U,
        rootTicksPerStep,
        fallback,
        rootTicksPerStep,
        tile.probabilityCycleActive && !graphPreviewAttempted
    );
}

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
    const auto& pattern =
        core::state::sequencer::authoringPattern(sequencer);
    frame.scaleSettings = context.scaleSettings;
    frame.chromaticPitchEditing =
        pattern.pitchEditMode ==
        core::state::sequencer::SequencerPitchEditMode::CHROMATIC;
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
        if (resolved.playheadVisible) {
            if (context.childContext) {
                tile.playheadProgress = context.contentPlayback.progress;
            } else {
                tile.playheadProgress = sequencer.playheadStepPhaseQ8.get();
            }
        }
        tile.contentBadges = buildStepContentBadgeProjectionForNode(
            pattern,
            resolved.nodeId
        );
        if (!context.childContext) {
            applyCycleStatePlaybackProjection(
                tile.contentBadges,
                pattern,
                resolved.nodeId,
                sequencer.probabilityCycleIndex
            );
        }
        if (!sequencer.contentView.isChildContent() &&
            tile.contentBadges.microLength > 0U &&
            resolved.playheadVisible) {
            applyMicroSequencePlaybackProjection(
                tile.contentBadges,
                resolved.gate,
                core::state::sequencer::authoringPattern(sequencer)
                    .stepsPerBeat.get(),
                sequencer.playheadStepTickOffset.get(),
                sequencer.expandedVariationTelemetry,
                absoluteStep
            );
        }
        mergeExpandedTelemetryChordBadgeForNode(
            tile.contentBadges,
            sequencer.expandedVariationTelemetry,
            resolved.runtimeNodeId,
            sequencer.playheadStep.get(),
            sequencer.playheadStepTickOffset.get()
        );
        if (!sequencer.contentView.isChildContent() &&
            sequencer.expandedVariationTelemetry.valid &&
            sequencer.expandedVariationTelemetry.noteBudgetExceeded &&
            sequencer.expandedVariationTelemetry.rootStepIndex == absoluteStep) {
            tile.contentBadges.expansionLimitReached = true;
        }
        populateTileNoteEvents(
            sequencer,
            pattern,
            context.scaleSettings,
            context.childContext,
            absoluteStep,
            stepInlineEditActive,
            tile
        );
        // A compact projection deliberately caps detailed events. Reuse the
        // existing expansion warning when the complete structure cannot be
        // represented, instead of silently hiding musical content.
        tile.contentBadges.expansionLimitReached =
            tile.contentBadges.expansionLimitReached || tile.noteEvents.dense;
    }

    frame.pitchViewport = buildStepPitchViewport(frame.tiles);

    return frame;
}

}  // namespace core::ui::sequencer::grid

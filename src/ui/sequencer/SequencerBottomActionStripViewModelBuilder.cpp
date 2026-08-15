#include "ui/sequencer/SequencerBottomActionStripViewModelBuilder.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "config/Timing.hpp"
#include "state/project/ProjectDomainRules.hpp"
#include "state/StructureNavigationState.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerInteractionContextOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerStepPastePlan.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "state/sequencer/DrumPatternState.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/SequencerActionStripVisuals.hpp"
#include "ui/sequencer/SequencerTrackPastePendingViewModel.hpp"
#include "ui/sequencer/SequencerTrackPastePreflightViewModel.hpp"
#include "ui/sequencer/SequencerTrackPasteProjection.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui::sequencer {
namespace structure_slots = core::state::shared;

namespace {

using StripProps = core::ui::ContextActionStripProps;
using SlotProps = core::ui::ContextActionStripSlotProps;
using Visual = core::ui::ContextActionStripVisualState;
using Tone = core::ui::ContextActionStripTone;
using InteractionAction = core::state::sequencer::SequencerInteractionAction;
using InteractionVisibility =
    core::state::sequencer::SequencerInteractionVisibility;

using TrackTransferProjection = SequencerTrackPasteProjection;

FLASHMEM uint8_t countSelectedItems(uint16_t mask) {
    uint8_t count = 0;
    while (mask != 0) {
        count += static_cast<uint8_t>(mask & 1U);
        mask >>= 1U;
    }
    return count;
}

FLASHMEM Visual interactionVisual(InteractionVisibility visibility) {
    switch (visibility) {
        case InteractionVisibility::ACTIVE:
            return Visual::ACTIVE;
        case InteractionVisibility::DISABLED:
            return Visual::DISABLED;
        case InteractionVisibility::HIDDEN:
        default:
            return Visual::HIDDEN;
    }
}

FLASHMEM uint8_t countSelectedSteps(
    oc::note::sequencer::StepBitMask128 mask,
    uint8_t limit
) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < limit; ++i) {
        if (mask.test(i)) ++count;
    }
    return count;
}

FLASHMEM TrackTransferProjection trackTransferProjection(
    const SequencerViewModelSource& source
) {
    return projectSequencerTrackPaste(source);
}

FLASHMEM bool trackPasteAvailable(const TrackTransferProjection& projection) {
    return core::state::contextual::canExecute(projection.action.hold);
}

FLASHMEM bool showPastePending(
    SlotProps& slot,
    const TrackTransferProjection& projection
) {
    const auto model = buildSequencerTrackPastePendingViewModel(projection.plan);
    if (!model.visible) return false;
    slot = core::ui::makeStandaloneIconStripSlot(
        interactionActionIcon(InteractionAction::PASTE_CURRENT_STRUCTURE),
        Visual::DISABLED,
        Tone::NEUTRAL
    );
    slot.showLabel = true;
    std::snprintf(slot.labelText.data(), slot.labelText.size(), "%s", model.label);
    return true;
}

FLASHMEM Tone trackPasteTone(const TrackTransferProjection& projection) {
    return projection.action.hold.visual.tone ==
               core::state::contextual::ContextTone::AMBER
        ? Tone::WARNING
        : Tone::POSITIVE;
}

FLASHMEM void formatSelectionLabel(
    std::array<char, 16>& out,
    uint8_t count
) {
    std::snprintf(
        out.data(),
        out.size(),
        "%u selected",
        static_cast<unsigned>(count)
    );
}

FLASHMEM SlotProps makeSelectionCountSlot(uint8_t selectedCount) {
    SlotProps slot{
        .visualState = Visual::ACTIVE,
        .tone = Tone::NEUTRAL,
        .showIcon = false,
        .icon = nullptr,
        .showLabel = true,
    };
    formatSelectionLabel(slot.labelText, selectedCount);
    return slot;
}

FLASHMEM void applyPastePlacementSlots(
    StripProps& props,
    uint8_t selectedCount,
    uint8_t overwriteCount,
    bool blocked,
    Visual pasteVisual
) {
    props.slots[0].visualState = Visual::HIDDEN;
    props.slots[1] = makeSelectionCountSlot(selectedCount);
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        standalone::icons::ACTION_PASTE,
        pasteVisual,
        blocked
            ? Tone::DESTRUCTIVE
            : overwriteCount > 0U
                ? Tone::WARNING
                : Tone::POSITIVE
    );
    if (overwriteCount == 0U) return;

    props.slots[2].showLabel = true;
    std::snprintf(
        props.slots[2].labelText.data(),
        props.slots[2].labelText.size(),
        "PST \xC2\xB7 %u OVR",
        static_cast<unsigned>(overwriteCount)
    );
}

FLASHMEM void applyHoldProgress(SlotProps& slot,
                       const core::state::StructureHoldState& holdState,
                       bool active) {
    slot.holdActive = active;
    slot.holdStartedAtMs = holdState.startedAtMs.get();
    slot.holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
}

FLASHMEM void applyTrackPasteProgress(
    SlotProps& slot,
    const core::state::contextual::GuardedActionState& guard
) {
    slot.holdActive =
        guard.phase == core::state::contextual::GuardedActionPhase::ARMED;
    slot.holdStartedAtMs = guard.pressedAtMs;
    slot.holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
}

FLASHMEM Tone variationStatusTone(
    core::state::sequencer::StepProperty property
) {
    switch (property) {
        case core::state::sequencer::StepProperty::VELOCITY:
            return Tone::WARNING;
        case core::state::sequencer::StepProperty::GATE:
            return Tone::POSITIVE;
        case core::state::sequencer::StepProperty::NUDGE:
            return Tone::CONSTRUCTIVE;
        case core::state::sequencer::StepProperty::NOTE:
        case core::state::sequencer::StepProperty::PROBABILITY:
        default:
            return Tone::NEUTRAL;
    }
}

FLASHMEM void formatVariationStatusLabel(std::array<char, 16>& out,
                                core::state::sequencer::StepProperty property,
                                uint8_t range) {
    constexpr const char* plusMinus = "\xC2\xB1";

    if (property == core::state::sequencer::StepProperty::PROBABILITY) {
        std::snprintf(out.data(), out.size(), "--");
        return;
    }

    std::snprintf(
        out.data(),
        out.size(),
        "%s%u",
        plusMinus,
        static_cast<unsigned>(range)
    );
}

FLASHMEM bool focusedStepHasChildContent(
    const SequencerViewModelSource& source
) {
    const auto& sequencer = source.sequencer;
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(
        sequencer,
        sequencer.focusedStep.get()
    );
    return core::state::sequencer::stepNodeHasAnyChildContent(
        core::state::sequencer::authoringPattern(sequencer),
        nodeId
    );
}

FLASHMEM bool canPasteStepContent(const SequencerViewModelSource& source) {
    return source.structureClipboard.hasSequencerStepContent(
               core::state::SequencerStepContentClipboardKind::ALL
           ) &&
           core::state::sequencer::activeContentStepCanReceiveChildContent(
               source.sequencer,
               source.sequencer.focusedStep.get()
           );
}

FLASHMEM bool canPasteDrumStep(const SequencerViewModelSource& source) {
    const auto& clipboard = source.structureClipboard;
    if (!clipboard.hasSequencerSteps() ||
        !clipboard.sequencerSteps.drumContext ||
        clipboard.sequencerSteps.count != 1U ||
        !clipboard.sequencerSteps.entries[0].valid) {
        return false;
    }
    const auto nodeId = clipboard.sequencerSteps.entries[0].sourceNodeId;
    if (nodeId ==
        oc::note::sequencer::StepSequencerGraphLimits::INVALID_ID) {
        return true;
    }
    return clipboard.sequencerGraph &&
        core::state::sequencer::inspectSequencerGraphPayload(
            *clipboard.sequencerGraph,
            nodeId,
            0U
        ).ok();
}

struct StepSelectionPasteProjection {
    core::state::sequencer::SequencerStepPastePreviewPlan plan{};
    uint8_t overwriteCount = 0U;
    bool compatibleClipboard = false;
    bool canPaste = false;
};

FLASHMEM StepSelectionPasteProjection stepSelectionPasteProjection(
    const SequencerViewModelSource& source
) {
    StepSelectionPasteProjection projection{};
    const auto& selection =
        source.sequencer.structureUi.stepSelection;
    projection.compatibleClipboard =
        selection.placementActive() &&
        selection.clipboardRevision.get() ==
            source.structureClipboard.revision.get() &&
        source.structureClipboard.hasSequencerSteps() &&
        !source.structureClipboard.sequencerSteps.drumContext &&
        source.structureClipboard.sequencerSteps.rootContext ==
            core::state::sequencer::isRootContentView(source.sequencer);
    if (!projection.compatibleClipboard) return projection;

    projection.plan =
        core::state::sequencer::buildStepPastePreviewPlan(
            source.structureClipboard.sequencerSteps,
            core::state::sequencer::isRootContentView(source.sequencer),
            selection.cursorStep.get(),
            core::state::sequencer::activeContentLength(source.sequencer),
            core::state::sequencer::maxStepCursorForPaste(source.sequencer),
            core::state::project::sanitizeProjectStepPasteMode(
                source.projectNavigation.stepPasteMode
            )
        );
    for (uint8_t index = 0U;
         index < projection.plan.count;
         ++index) {
        if (projection.plan.entries[index].valid &&
            projection.plan.entries[index].preview ==
                core::state::sequencer::
                    SequencerStepPastePreview::OVERWRITE) {
            ++projection.overwriteCount;
        }
    }
    projection.canPaste =
        !projection.plan.blocked &&
        projection.plan.hasEntries();
    return projection;
}

FLASHMEM core::state::sequencer::SequencerInteractionContext
makeBottomInteractionContext(
    const SequencerViewModelSource& source,
    const StepSelectionPasteProjection* stepPaste = nullptr
) {
    auto context = core::state::sequencer::makeSequencerInteractionContext(
        source.sequencer,
        source.trackNavigation,
        source.navigationFocus.get()
    );
    const bool trackFocus =
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;

    if (context.trackSelectionActive) {
        context.selectedItemsAvailable =
            (source.trackNavigation.selection.selectedMask.get() &
             source.sharedTrackEnabledMask.get()) != 0U;
        context.selectionPlacementActive =
            source.trackNavigation.selection.placementActive();
        context.selectionPasteAvailable =
            context.selectionPlacementActive &&
            !source.trackNavigation.selection.pasteBlocked.get() &&
            source.trackNavigation.selection.destinationMask.get() != 0U;
        return context;
    }
    if (context.pageSelectionActive) {
        context.selectedItemsAvailable =
            (source.sequencer.structureUi.pageSelection.selectedMask.get() &
             structure_slots::prefixMask(
                 core::state::sequencer::activeContentPageCount(
                     source.sequencer
                 )
             )) != 0U;
        context.selectionPlacementActive =
            source.sequencer.structureUi.pageSelection.placementActive();
        context.selectionPasteAvailable =
            context.selectionPlacementActive &&
            !source.sequencer.structureUi.pageSelection.pasteBlocked.get() &&
            source.sequencer.structureUi.pageSelection.destinationMask.get() != 0U;
        return context;
    }
    if (context.stepSelectionActive) {
        context.selectedItemsAvailable =
            countSelectedSteps(
                source.sequencer.structureUi.stepSelection.selectedMask.get(),
                core::state::sequencer::activeContentLength(source.sequencer)
            ) > 0;
        context.selectionPlacementActive =
            source.sequencer.structureUi.stepSelection.placementActive();
        context.selectionPasteAvailable =
            stepPaste != nullptr
            ? stepPaste->canPaste
            : stepSelectionPasteProjection(source).canPaste;
        context.compatibleClipboardAvailable =
            context.selectionPasteAvailable;
        return context;
    }
    if (context.drumLaneSelectionActive) {
        const auto& selection =
            source.sequencer.drumSequencer.laneSelection;
        context.selectedItemsAvailable = selection.anySelected();
        context.selectionPlacementActive = selection.placementActive();
        context.selectionPasteAvailable =
            selection.placementActive() &&
            !selection.pasteBlocked &&
            selection.destinationMask != 0U &&
            selection.clipboardRevision ==
                source.structureClipboard.revision.get() &&
            source.structureClipboard.hasSequencerDrumLaneSelection();
        return context;
    }

    if (source.navigationFocus.get() == core::state::StructureNavigationFocus::STEP) {
        const bool focusedStepValid =
            source.sequencer.focusedStep.get() <
            core::state::sequencer::activeContentLength(source.sequencer);
        context.currentStructureCanClear = focusedStepValid;
        context.currentStructureCanRemove = focusedStepValid;
        context.currentStructureCanCopy = focusedStepValid;
        context.compatibleClipboardAvailable =
            source.structureClipboard.hasSequencerSteps() &&
            !source.structureClipboard.sequencerSteps.drumContext &&
            source.structureClipboard.sequencerSteps.rootContext ==
                core::state::sequencer::isRootContentView(source.sequencer);
        return context;
    }

    context.currentStructureCanClear = !context.previewingAddSlot;
    context.currentStructureCanCopy = !context.previewingAddSlot;

    if (context.childContentView) {
        context.currentStepHasChildContent = focusedStepHasChildContent(source);
        context.compatibleClipboardAvailable = canPasteStepContent(source);
        return context;
    }
    if (trackFocus) {
        context.currentStructureCanRemove =
            !context.previewingAddSlot &&
            countSelectedItems(source.sharedTrackEnabledMask.get()) > 1;
        context.compatibleClipboardAvailable =
            trackPasteAvailable(trackTransferProjection(source));
    } else {
        context.currentStructureCanRemove =
            !context.previewingAddSlot && source.sequencer.activePageCount() > 1;
        context.compatibleClipboardAvailable = source.structureClipboard.hasSequencerPage();
    }
    return context;
}

}  // namespace

FLASHMEM ContextActionStripProps buildSequencerBottomActionStripProps(
    const SequencerViewModelSource& source
) {
    StripProps props;
    props.visible = true;
    if (core::state::sequencer::isDrumOverviewActive(source.sequencer) ||
        (source.sequencer.drumSequencer.active() &&
         !source.sequencer.drumSequencer.gridVisible())) {
        const auto& drumUi = source.sequencer.drumSequencer;
        if (!drumUi.gridVisible()) return props;
        if (drumUi.selectorVisible()) return props;
        if (drumUi.laneAddSlotFocused()) return props;
        if (drumUi.laneSelection.active) {
            const auto& selection = drumUi.laneSelection;
            const uint8_t selectedCount = countSelectedItems(
                selection.selectedMask
            );
            const auto interaction =
                core::state::sequencer::buildSequencerInteractionPolicy(
                    makeBottomInteractionContext(source)
                );
            if (selection.moveActive()) {
                props.slots[0].visualState = Visual::HIDDEN;
                props.slots[1] = makeSelectionCountSlot(selectedCount);
                props.slots[2] = core::ui::makeStandaloneIconStripSlot(
                    standalone::icons::ACTION_APPLY,
                    Visual::ACTIVE,
                    Tone::POSITIVE
                );
                return props;
            }
            if (selection.placementActive()) {
                const uint8_t overwriteCount = countSelectedItems(
                    selection.overwriteMask
                );
                const bool canPaste = !selection.pasteBlocked &&
                    selection.destinationMask != 0U;
                const auto& hold = source.sequencer.structureUi.pageHold;
                const bool holdActive = canPaste &&
                    hold.action.get() ==
                        core::state::StructureHoldAction::PASTE;
                applyPastePlacementSlots(
                    props,
                    selectedCount,
                    overwriteCount,
                    selection.pasteBlocked,
                    holdActive
                        ? Visual::ARMED
                        : interactionVisual(interaction.bottomRightVisibility)
                );
                applyHoldProgress(props.slots[2], hold, holdActive);
                return props;
            }
            props.slots[0] = core::ui::makeStandaloneIconStripSlot(
                interactionActionIcon(InteractionAction::CLEAR_SELECTION),
                selectedCount > 0U ? Visual::ACTIVE : Visual::DISABLED,
                Tone::WARNING
            );
            props.slots[1] = makeSelectionCountSlot(selectedCount);
            props.slots[2] = core::ui::makeStandaloneIconStripSlot(
                interactionActionIcon(
                    InteractionAction::COPY_STRUCTURE_SELECTION
                ),
                interactionVisual(interaction.bottomRightVisibility),
                Tone::NEUTRAL
            );
            return props;
        }
        const auto focus = source.navigationFocus.get();
        if (focus == core::state::StructureNavigationFocus::TRACK) {
            // Track Mute/Remove/Copy/Paste is domain-agnostic and already
            // snapshots the complete Drum payload. Let the common builder
            // project exactly the same contract as an Instrument Track.
        } else if (focus == core::state::StructureNavigationFocus::STEP) {
            const auto& hold = source.sequencer.structureUi.pageHold;
            const bool resetHold = hold.action.get() ==
                core::state::StructureHoldAction::REMOVE;
            const bool pasteAvailable = canPasteDrumStep(source);
            const bool pasteHold = pasteAvailable &&
                hold.action.get() == core::state::StructureHoldAction::PASTE;

            props.slots[0] = core::ui::makeStandaloneIconStripSlot(
                interactionActionIcon(
                    resetHold
                        ? InteractionAction::RESET_CURRENT_STEP_DEEP
                        : InteractionAction::RESET_CURRENT_STEP_SHALLOW
                ),
                resetHold ? Visual::ARMED : Visual::ACTIVE,
                resetHold ? Tone::DESTRUCTIVE : Tone::WARNING
            );
            applyHoldProgress(props.slots[0], hold, resetHold);
            props.slots[1].visualState = Visual::HIDDEN;
            props.slots[2] = core::ui::makeStandaloneIconStripSlot(
                interactionActionIcon(
                    pasteHold
                        ? InteractionAction::PASTE_CURRENT_STEP
                        : InteractionAction::COPY_CURRENT_STEP
                ),
                pasteHold ? Visual::ARMED : Visual::ACTIVE,
                pasteHold ? Tone::POSITIVE : Tone::NEUTRAL
            );
            applyHoldProgress(props.slots[2], hold, pasteHold);
            return props;
        } else {
        const uint8_t length = drumUi.drumTrack->pattern.effectiveLength(
            drumUi.selectedLane
        );
        const uint8_t pageCount = std::max<uint8_t>(
            1U,
            static_cast<uint8_t>(
                (length + drumUi.STEPS_PER_PAGE - 1U) /
                drumUi.STEPS_PER_PAGE
            )
        );
        const Visual pagingVisual = pageCount > 1U
            ? Visual::ACTIVE
            : Visual::DISABLED;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_BACKWARD,
            pagingVisual,
            Tone::NEUTRAL
        );
        // BOTTOM_CENTER remains the global Transport control. Keeping this
        // slot empty avoids presenting OPT as a button action.
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_BACKWARD,
            pagingVisual,
            Tone::NEUTRAL
        );
        props.slots[2].iconRotated180 = true;
        return props;
        }
    }

    const bool trackFocus =
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool selectingTrack =
        source.trackNavigation.selection.active.get();
    const bool selectingPage =
        source.sequencer.structureUi.pageSelection.active.get();
    const bool selectingStep = source.sequencer.structureUi.stepSelection.active.get();
    const bool selectingPatternVariation =
        source.sequencer.stepPropertyInlineSelector.selecting.get();
    const bool selectingState = selectingPatternVariation &&
        source.sequencer.stepStatePropertyActive.get();

    if (source.sequencer.stepContentDraft.active.get() &&
        core::state::sequencer::isChildContentView(source.sequencer)) {
        const auto draftContext = makeBottomInteractionContext(source);
        if (draftContext.stepEditorValueRowFocused) {
            // Reset is a draft-local edit and remains safe: it mutates only the
            // unpublished authoring state. Structural Clear/Remove stays
            // hidden, while Discard remains the exact transaction rollback.
            props.slots[0] = core::ui::makeStandaloneIconStripSlot(
                interactionActionIcon(InteractionAction::RESET_STEP_EDITOR_ROW),
                Visual::ACTIVE,
                Tone::NEUTRAL
            );
        } else {
            props.slots[0].visualState = Visual::HIDDEN;
        }
        props.slots[1].visualState = Visual::HIDDEN;
        if (source.sequencer.stepContentDraft.exitPromptVisible.get()) {
            props.slots[2].visualState = Visual::HIDDEN;
        } else {
            props.slots[2] = core::ui::makeStandaloneIconStripSlot(
                standalone::icons::ACTION_VALIDATE,
                Visual::ACTIVE,
                Tone::POSITIVE
            );
        }
        return props;
    }

    if (source.sequencer.ccLaneUi.mode ==
        core::state::sequencer::SequencerCcLaneUiMode::LANE_GRID) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_CLEAR,
            source.sequencer.ccLaneUi.hasAuthoredValue
                ? Visual::ACTIVE
                : Visual::DISABLED
        );
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::SETTINGS_GEAR,
            Visual::ACTIVE
        );
        return props;
    }

    if (selectingState) {
        props.slots[0].visualState = Visual::HIDDEN;
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    if (selectingPatternVariation) {
        const auto property = source.sequencer.activeStepProperty.get();
        const uint8_t range = source.sequencer.variationRangeForProperty(property);
        const char* propertyIcon = visual::propertyIconGlyph(property);
        const bool canOpenPitchSettings =
            property == core::state::sequencer::StepProperty::NOTE;

        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::SETTINGS_GEAR,
            canOpenPitchSettings ? Visual::ACTIVE : Visual::HIDDEN
        );
        props.slots[1] = SlotProps{
            .visualState = property == core::state::sequencer::StepProperty::PROBABILITY
                ? Visual::DISABLED
                : Visual::ACTIVE,
            .tone = variationStatusTone(property),
            .showIcon = true,
            .icon = propertyIcon,
            .iconUsesStandaloneFont = true,
            .iconSize = standalone::icons::Size::L,
            .showLabel = true,
            .label = nullptr,
        };
        formatVariationStatusLabel(props.slots[1].labelText, property, range);
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    const auto stepPaste = selectingStep
        ? stepSelectionPasteProjection(source)
        : StepSelectionPasteProjection{};
    const auto bottomContext =
        makeBottomInteractionContext(source, &stepPaste);
    const auto interaction = core::state::sequencer::buildSequencerInteractionPolicy(
        bottomContext
    );

    if (selectingTrack || selectingPage) {
        const auto& selection = selectingTrack
            ? source.trackNavigation.selection
            : source.sequencer.structureUi.pageSelection;
        const uint8_t itemCount = selectingTrack
            ? core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
            : core::state::sequencer::activeContentPageCount(source.sequencer);
        const uint16_t availableMask = selectingTrack
            ? source.sharedTrackEnabledMask.get()
            : structure_slots::prefixMask(itemCount);
        const uint16_t selectionMask = static_cast<uint16_t>(
            selection.selectedMask.get() &
            availableMask
        );
        const uint8_t selectedCount =
            countSelectedItems(selectionMask);
        const bool placing = selection.placementActive();
        if (placing) {
            const uint8_t overwriteCount =
                countSelectedItems(selection.overwriteMask.get());
            const bool blocked = selection.pasteBlocked.get();
            const bool canPaste = !blocked &&
                selection.destinationMask.get() != 0U;
            const auto& holdState = selectingTrack
                ? source.trackNavigation.hold
                : source.sequencer.structureUi.pageHold;
            const bool pageHoldActive =
                !selectingTrack &&
                holdState.action.get() ==
                    core::state::StructureHoldAction::PASTE &&
                canPaste;
            const auto& trackPaste =
                source.sequencer.structureUi.trackPaste;
            const bool trackHoldActive =
                selectingTrack &&
                trackPaste.buttonOwned &&
                trackPaste.guard.phase !=
                    core::state::contextual::GuardedActionPhase::IDLE &&
                trackPaste.guard.phase !=
                    core::state::contextual::GuardedActionPhase::CANCELLED;

            applyPastePlacementSlots(
                props,
                selectedCount,
                overwriteCount,
                blocked,
                (trackHoldActive || pageHoldActive)
                    ? Visual::ARMED
                    : interactionVisual(interaction.bottomRightVisibility)
            );
            if (selectingTrack) {
                applyTrackPasteProgress(
                    props.slots[2],
                    trackPaste.guard
                );
            } else {
                applyHoldProgress(
                    props.slots[2],
                    holdState,
                    pageHoldActive
                );
            }
            return props;
        }

        const uint8_t availableCount =
            countSelectedItems(availableMask);
        const bool canTap = selectedCount > 0U;
        const bool deletesStructure =
            selectingTrack ||
            core::state::sequencer::isRootContentView(source.sequencer);
        const bool canHold = deletesStructure
            ? selectedCount > 0U && selectedCount < availableCount
            : selectedCount > 0U;
        const auto& holdState = selectingTrack
            ? source.trackNavigation.hold
            : source.sequencer.structureUi.pageHold;
        const bool holdActive =
            holdState.action.get() ==
                core::state::StructureHoldAction::REMOVE &&
            canHold;
        const auto displayedAction = holdActive
            ? interaction.bottomLeftHold
            : interaction.bottomLeftTap;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(displayedAction),
            holdActive
                ? Visual::ARMED
                : (canTap ? Visual::ACTIVE : Visual::DISABLED),
            holdActive
                ? Tone::DESTRUCTIVE
                : (selectingPage ? Tone::WARNING : Tone::NEUTRAL)
        );
        applyHoldProgress(props.slots[0], holdState, holdActive);
        props.slots[1] = makeSelectionCountSlot(selectedCount);
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(
                InteractionAction::COPY_STRUCTURE_SELECTION
            ),
            interactionVisual(interaction.bottomRightVisibility),
            Tone::NEUTRAL
        );
        return props;
    }

    if (selectingStep) {
        const auto& selection =
            source.sequencer.structureUi.stepSelection;
        const uint8_t selectedCount =
            countSelectedSteps(
                selection.selectedMask.get(),
                core::state::sequencer::activeContentLength(source.sequencer)
            );
        if (selection.placementActive()) {
            const uint8_t overwriteCount =
                stepPaste.overwriteCount;
            const bool blocked = !stepPaste.canPaste;
            const auto& holdState =
                source.sequencer.structureUi.pageHold;
            const bool pasteHoldActive =
                !blocked &&
                holdState.action.get() ==
                    core::state::StructureHoldAction::PASTE;

            applyPastePlacementSlots(
                props,
                selectedCount,
                overwriteCount,
                blocked,
                pasteHoldActive
                    ? Visual::ARMED
                    : interactionVisual(interaction.bottomRightVisibility)
            );
            if (blocked) {
                props.slots[2].showLabel = true;
                std::snprintf(
                    props.slots[2].labelText.data(),
                    props.slots[2].labelText.size(),
                    "PST BLOCK"
                );
            }
            applyHoldProgress(
                props.slots[2],
                holdState,
                pasteHoldActive
            );
            return props;
        }

        const bool canClear = selectedCount > 0;
        const bool canPaste =
            interaction.bottomRightHold ==
            InteractionAction::PASTE_STEP_SELECTION;
        const bool canCopy =
            interaction.bottomRightTap ==
            InteractionAction::COPY_STEP_SELECTION;
        const bool pastePreviewActive = false;
        const auto& holdState = source.sequencer.structureUi.pageHold;
        const auto holdAction = holdState.action.get();
        const bool removeHoldActive =
            holdAction == core::state::StructureHoldAction::REMOVE && canClear;
        const bool pasteHoldActive =
            holdAction == core::state::StructureHoldAction::PASTE && canPaste;
        const auto rightAction = pasteHoldActive || pastePreviewActive || (!canCopy && canPaste)
            ? interaction.bottomRightHold
            : interaction.bottomRightTap;
        const auto leftAction = interaction.bottomLeftHold;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(leftAction),
            removeHoldActive ? Visual::ARMED : (canClear ? Visual::ACTIVE : Visual::DISABLED),
            removeHoldActive ? Tone::DESTRUCTIVE : Tone::WARNING
        );
        applyHoldProgress(props.slots[0], holdState, removeHoldActive);
        props.slots[1] = makeSelectionCountSlot(selectedCount);
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(rightAction),
            pasteHoldActive
                ? Visual::ARMED
                : interactionVisual(interaction.bottomRightVisibility),
            (pasteHoldActive || pastePreviewActive) ? Tone::POSITIVE : Tone::NEUTRAL
        );
        applyHoldProgress(props.slots[2], holdState, pasteHoldActive);
        return props;
    }

    if (core::state::sequencer::isChildContentView(source.sequencer)) {
        const bool hasChildContent = bottomContext.currentStepHasChildContent;
        const bool canPaste = bottomContext.compatibleClipboardAvailable;
        const auto rightAction = hasChildContent || !canPaste
            ? interaction.bottomRightTap
            : interaction.bottomRightHold;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(interaction.bottomLeftTap),
            hasChildContent ? Visual::ACTIVE : Visual::DISABLED,
            Tone::DESTRUCTIVE
        );
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(rightAction),
            (hasChildContent || canPaste) ? Visual::ACTIVE : Visual::DISABLED,
            (!hasChildContent && canPaste) ? Tone::POSITIVE : Tone::NEUTRAL
        );
        return props;
    }

    const bool canClear =
        interaction.bottomLeftTap != InteractionAction::NONE &&
        bottomContext.currentStructureCanClear;
    const bool canRemove =
        interaction.bottomLeftHold != InteractionAction::NONE &&
        bottomContext.currentStructureCanRemove;
    const bool canCopy =
        interaction.bottomRightTap != InteractionAction::NONE &&
        bottomContext.currentStructureCanCopy;
    const auto trackProjection = trackFocus
        ? trackTransferProjection(source)
        : TrackTransferProjection{};
    const bool canPaste = interaction.bottomRightHold != InteractionAction::NONE &&
        (trackFocus ? trackPasteAvailable(trackProjection)
                    : bottomContext.compatibleClipboardAvailable);
    const bool pasteOverwritesDestination = canPaste && !bottomContext.previewingAddSlot;
    const bool copyOrPasteAvailable = canCopy || canPaste;
    const auto& holdState = trackFocus ? source.trackNavigation.hold
                                       : source.sequencer.structureUi.pageHold;
    const auto holdAction = holdState.action.get();
    const bool removeHoldActive =
        holdAction == core::state::StructureHoldAction::REMOVE && canRemove;
    const bool pastePressed = trackFocus &&
        trackProjection.guard.phase ==
            core::state::contextual::GuardedActionPhase::PRESSED;
    const bool pasteHoldActive = trackFocus
        ? trackProjection.guard.phase ==
              core::state::contextual::GuardedActionPhase::ARMED
        : holdAction == core::state::StructureHoldAction::PASTE && canPaste;
    const auto leftAction = removeHoldActive
        ? interaction.bottomLeftHold
        : interaction.bottomLeftTap;
    const auto rightAction = (pasteHoldActive || (!canCopy && canPaste))
        ? interaction.bottomRightHold
        : interaction.bottomRightTap;

    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        interactionActionIcon(leftAction),
        leftAction == InteractionAction::NONE
            ? Visual::HIDDEN
            : (removeHoldActive
                   ? Visual::ARMED
                   : ((canClear || canRemove) ? Visual::ACTIVE : Visual::HIDDEN)),
        removeHoldActive ? Tone::DESTRUCTIVE : Tone::WARNING
    );
    applyHoldProgress(props.slots[0], holdState, removeHoldActive);
    props.slots[1].visualState = Visual::HIDDEN;
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        interactionActionIcon(rightAction),
        rightAction == InteractionAction::NONE
            ? Visual::HIDDEN
            : (pasteHoldActive
                   ? Visual::ARMED
                   : (pastePressed
                          ? Visual::PRESSED
                          : (copyOrPasteAvailable ? Visual::ACTIVE
                                                  : Visual::DISABLED))),
        pasteHoldActive
            ? (trackFocus ? trackPasteTone(trackProjection)
                          : (pasteOverwritesDestination ? Tone::WARNING
                                                       : Tone::POSITIVE))
            : Tone::NEUTRAL
    );
    if (trackFocus) {
        showPastePending(props.slots[2], trackProjection);
        applyTrackPasteProgress(props.slots[2], trackProjection.guard);
    } else {
        applyHoldProgress(props.slots[2], holdState, pasteHoldActive);
    }
    return props;
}

}  // namespace core::ui::sequencer

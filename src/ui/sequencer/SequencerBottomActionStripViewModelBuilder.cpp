#include "ui/sequencer/SequencerBottomActionStripViewModelBuilder.hpp"

#include <array>
#include <cstdint>
#include <cstdio>

#include <config/PlatformCompat.hpp>

#include "config/Timing.hpp"
#include "state/StructureSelectionState.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerInteractionContextOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/SequencerActionStripVisuals.hpp"
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

uint8_t countSelectedItems(uint16_t mask) {
    uint8_t count = 0;
    while (mask != 0) {
        count += static_cast<uint8_t>(mask & 1U);
        mask >>= 1U;
    }
    return count;
}

uint8_t countSelectedSteps(oc::note::sequencer::StepBitMask128 mask, uint8_t limit) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < limit; ++i) {
        if (mask.test(i)) ++count;
    }
    return count;
}

void formatSelectionLabel(std::array<char, 16>& out, uint8_t count) {
    std::snprintf(
        out.data(),
        out.size(),
        "SEL %u",
        static_cast<unsigned>(count)
    );
}

SlotProps makeSelectionCountSlot(uint8_t selectedCount) {
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

void applyHoldProgress(SlotProps& slot,
                       const core::state::StructureHoldState& holdState,
                       bool active) {
    slot.holdActive = active;
    slot.holdStartedAtMs = holdState.startedAtMs.get();
    slot.holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
}

Tone variationStatusTone(core::state::sequencer::StepProperty property) {
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

void formatVariationStatusLabel(std::array<char, 16>& out,
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

bool focusedStepHasChildContent(const SequencerViewModelSource& source) {
    const auto& sequencer = source.sequencer;
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(
        sequencer,
        sequencer.focusedStep.get()
    );
    return core::state::sequencer::stepNodeHasAnyChildContent(sequencer.pattern, nodeId);
}

bool canPasteStepContent(const SequencerViewModelSource& source) {
    return source.structureClipboard.hasSequencerStepContent(
               core::state::SequencerStepContentClipboardKind::ALL
           ) &&
           core::state::sequencer::activeContentStepCanReceiveChildContent(
               source.sequencer,
               source.sequencer.focusedStep.get()
           );
}

core::state::sequencer::SequencerInteractionContext makeBottomInteractionContext(
    const SequencerViewModelSource& source
) {
    auto context = core::state::sequencer::makeSequencerInteractionContext(
        source.sequencer,
        source.trackNavigation,
        source.navigationFocus.get()
    );
    const bool trackFocus =
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;

    if (context.stepSelectionActive) {
        context.selectedItemsAvailable =
            countSelectedSteps(
                source.sequencer.structureUi.stepSelection.selectedMask.get(),
                core::state::sequencer::activeContentLength(source.sequencer)
            ) > 0;
        context.compatibleClipboardAvailable =
            source.structureClipboard.hasSequencerSteps() &&
            source.structureClipboard.sequencerSteps.rootContext ==
                core::state::sequencer::isRootContentView(source.sequencer);
        return context;
    }

    if (context.trackSelectionActive) {
        const uint16_t actionableMask = static_cast<uint16_t>(
            source.trackNavigation.selection.selectedMask.get() &
            source.sharedTrackEnabledMask.get()
        );
        context.selectedItemsAvailable = countSelectedItems(actionableMask) > 0;
        context.compatibleClipboardAvailable =
            source.structureClipboard.hasSequencerTrackSelection();
        return context;
    }

    if (context.pageSelectionActive) {
        const uint16_t actionableMask = static_cast<uint16_t>(
            source.sequencer.structureUi.pageSelection.selectedMask.get() &
            structure_slots::prefixMask(source.sequencer.activePageCount())
        );
        context.selectedItemsAvailable = countSelectedItems(actionableMask) > 0;
        context.compatibleClipboardAvailable =
            source.structureClipboard.hasSequencerPageSelection();
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
        context.compatibleClipboardAvailable = source.structureClipboard.hasSequencerTrack();
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
    const bool trackFocus =
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool selectingTrack = source.trackNavigation.selection.active.get();
    const bool selectingPage = source.sequencer.structureUi.pageSelection.active.get();
    const bool selectingStep = source.sequencer.structureUi.stepSelection.active.get();
    const bool selectingPatternVariation =
        source.sequencer.stepPropertyInlineSelector.selecting.get();
    const uint16_t selectionMask = selectingTrack
        ? source.trackNavigation.selection.selectedMask.get()
        : source.sequencer.structureUi.pageSelection.selectedMask.get();

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

    const auto bottomContext = makeBottomInteractionContext(source);
    const auto interaction = core::state::sequencer::buildSequencerInteractionPolicy(
        bottomContext
    );

    if (selectingStep) {
        const uint8_t selectedCount =
            countSelectedSteps(
                source.sequencer.structureUi.stepSelection.selectedMask.get(),
                core::state::sequencer::activeContentLength(source.sequencer)
            );
        const bool canClear = selectedCount > 0;
        const bool canPaste =
            source.structureClipboard.hasSequencerSteps() &&
            source.structureClipboard.sequencerSteps.rootContext ==
                core::state::sequencer::isRootContentView(source.sequencer);
        const bool canCopy = selectedCount > 0;
        const bool pastePreviewActive =
            source.sequencer.structureUi.stepSelection.pastePreviewActive.get() && canPaste;
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
                : ((canCopy || canPaste) ? Visual::ACTIVE : Visual::DISABLED),
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

    if (selectingTrack || selectingPage) {
        bool canDeleteSelection = false;
        bool canCopySelection = false;
        bool canPasteSelection = false;
        if (selectingTrack) {
            const uint16_t enabledMask = source.sharedTrackEnabledMask.get();
            const uint16_t actionableMask = static_cast<uint16_t>(selectionMask & enabledMask);
            const uint8_t actionableCount = countSelectedItems(actionableMask);
            const uint8_t enabledCount = countSelectedItems(enabledMask);
            canDeleteSelection = actionableCount > 0 && actionableCount < enabledCount;
            canCopySelection = actionableCount > 0;
            canPasteSelection = source.structureClipboard.hasSequencerTrackSelection();
        } else {
            const uint8_t activePages = source.sequencer.activePageCount();
            const uint16_t actionableMask = static_cast<uint16_t>(
                selectionMask & structure_slots::prefixMask(activePages)
            );
            const uint8_t actionableCount = countSelectedItems(actionableMask);
            canDeleteSelection = actionableCount > 0 && actionableCount < activePages;
            canCopySelection = actionableCount > 0;
            canPasteSelection = source.structureClipboard.hasSequencerPageSelection();
        }

        const auto& holdState = selectingTrack ? source.trackNavigation.hold
                                               : source.sequencer.structureUi.pageHold;
        const bool deleteHoldActive =
            holdState.action.get() == core::state::StructureHoldAction::REMOVE &&
            canDeleteSelection;
        const bool pasteHoldActive =
            holdState.action.get() == core::state::StructureHoldAction::PASTE &&
            canPasteSelection;
        const auto rightAction = pasteHoldActive || (!canCopySelection && canPasteSelection)
            ? interaction.bottomRightHold
            : interaction.bottomRightTap;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(interaction.bottomLeftHold),
            deleteHoldActive
                ? Visual::ARMED
                : (canDeleteSelection ? Visual::ACTIVE : Visual::DISABLED),
            Tone::DESTRUCTIVE
        );
        applyHoldProgress(props.slots[0], holdState, deleteHoldActive);
        props.slots[1] = makeSelectionCountSlot(countSelectedItems(selectionMask));
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(rightAction),
            pasteHoldActive
                ? Visual::ARMED
                : ((canCopySelection || canPasteSelection) ? Visual::ACTIVE : Visual::DISABLED),
            pasteHoldActive ? Tone::POSITIVE : Tone::NEUTRAL
        );
        applyHoldProgress(props.slots[2], holdState, pasteHoldActive);
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
    const bool canPaste =
        interaction.bottomRightHold != InteractionAction::NONE &&
        bottomContext.compatibleClipboardAvailable;
    const bool pasteOverwritesDestination = canPaste && !bottomContext.previewingAddSlot;
    const bool copyOrPasteAvailable = canCopy || canPaste;
    const auto& holdState = trackFocus ? source.trackNavigation.hold
                                       : source.sequencer.structureUi.pageHold;
    const auto holdAction = holdState.action.get();
    const bool removeHoldActive =
        holdAction == core::state::StructureHoldAction::REMOVE && canRemove;
    const bool pasteHoldActive =
        holdAction == core::state::StructureHoldAction::PASTE && canPaste;
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
                   : (copyOrPasteAvailable ? Visual::ACTIVE : Visual::DISABLED)),
        pasteHoldActive
            ? (pasteOverwritesDestination ? Tone::WARNING : Tone::POSITIVE)
            : Tone::NEUTRAL
    );
    applyHoldProgress(props.slots[2], holdState, pasteHoldActive);
    return props;
}

}  // namespace core::ui::sequencer

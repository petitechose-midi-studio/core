#include "ui/sequencer/SequencerViewModelBuilder.hpp"

#include <oc/type/TextFormat.hpp>

#include "config/Timing.hpp"
#include "state/sequencer/SequencerPageSelectionPlan.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepGridFrameLogic.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

#include <cstdio>

namespace core::ui::sequencer {

namespace {

using StripProps = core::ui::ContextActionStripProps;
using SlotProps = core::ui::ContextActionStripSlotProps;
using Visual = core::ui::ContextActionStripVisualState;
using Tone = core::ui::ContextActionStripTone;

SlotProps makeIconSlot(const char* icon,
                       Visual visual,
                       Tone tone = Tone::NEUTRAL,
                       standalone::icons::Size iconSize = standalone::icons::Size::M) {
    return {
        .visualState = visual,
        .tone = tone,
        .showIcon = true,
        .icon = icon,
        .iconUsesStandaloneFont = true,
        .iconSize = iconSize,
        .showLabel = false,
        .label = nullptr,
    };
}

uint8_t countSelectedItems(uint16_t mask) {
    uint8_t count = 0;
    while (mask != 0) {
        count += static_cast<uint8_t>(mask & 1U);
        mask >>= 1U;
    }
    return count;
}

const char* clipboardBadge(const core::state::StructureClipboardState& clipboard) {
    switch (clipboard.kind.get()) {
        case core::state::StructureClipboardKind::SEQUENCER_PAGE:
            return "PG";
        case core::state::StructureClipboardKind::SEQUENCER_TRACK:
            return "TRK";
        default:
            return "";
    }
}

uint16_t pageBit(uint8_t page) {
    if (page >= core::state::sequencer::SequencerState::PAGE_COUNT) return 0;
    return static_cast<uint16_t>(1U << page);
}

uint16_t activePageMask(uint8_t pageCount) {
    if (pageCount >= core::state::sequencer::SequencerState::PAGE_COUNT) {
        return static_cast<uint16_t>(
            (1U << core::state::sequencer::SequencerState::PAGE_COUNT) - 1U
        );
    }
    return static_cast<uint16_t>((1U << pageCount) - 1U);
}

}  // namespace

SequencerHeaderBarProps buildHeaderBarProps(const SequencerViewModelSource& source) {
    const auto& sequencer = source.sequencer;
    const uint8_t activeTrack = source.sharedTrackActive.get();
    const bool focusingTrack =
        !source.trackNavigation.selection.active.get() &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool selectingTrack =
        source.trackNavigation.selection.active.get() &&
        source.trackNavigation.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool selectingPage =
        sequencer.structureUi.pageSelection.active.get() &&
        sequencer.structureUi.pageSelection.scope.get() == core::state::StructureSelectionScope::PAGE;
    const uint16_t pageSelectionMask = sequencer.structureUi.pageSelection.selectedMask.get();
    const uint16_t selectionMask = selectingTrack
        ? source.trackNavigation.selection.selectedMask.get()
        : (selectingPage ? pageSelectionMask : 0U);
    const uint8_t selectionCount = countSelectedItems(selectionMask);
    const bool previewAddTrackSlot =
        !source.trackNavigation.selection.active.get() && source.trackNavigation.previewAddSlot.get();
    const bool previewAddPageSlot =
        !sequencer.structureUi.pageSelection.active.get() &&
        sequencer.structureUi.previewAddPageSlot.get();
    const uint8_t addTrackIndex =
        (previewAddTrackSlot &&
         source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK)
            ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
                  source.trackNavigation.previewTrackIndex.get()
              )
            : core::ui::SequencerHeaderBarProps::TRACK_COUNT;
    const uint8_t previewTrack =
        selectingTrack
            ? source.trackNavigation.selection.cursorIndex.get()
            : ((previewAddTrackSlot &&
                addTrackIndex < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT)
                   ? addTrackIndex
                   : activeTrack);
    const uint8_t addPageIndex =
        (previewAddPageSlot &&
         source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE)
            ? sequencer.clampPage(sequencer.structureUi.previewPageIndex.get())
            : core::state::sequencer::SequencerState::PAGE_COUNT;
    const uint8_t viewedPage =
        selectingPage
            ? sequencer.structureUi.pageSelection.cursorIndex.get()
            : ((previewAddPageSlot && addPageIndex < core::state::sequencer::SequencerState::PAGE_COUNT)
                   ? addPageIndex
                   : sequencer.visiblePage());
    const bool previewPageAddSlotActive =
        previewAddPageSlot &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE &&
        addPageIndex < core::state::sequencer::SequencerState::PAGE_COUNT;
    const auto pageDuplicatePlan = selectingPage
        ? core::state::sequencer::buildPageDuplicatePlan(
              sequencer,
              pageSelectionMask,
              viewedPage
          )
        : core::state::sequencer::SequencerPageDuplicatePlan{};
    const bool pageDuplicateHasUsableDestination = pageDuplicatePlan.movesAnyPage();
    const bool pageClipboardPreview =
        !selectingPage &&
        !selectingTrack &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE &&
        source.structureClipboard.hasSequencerPage();
    const uint16_t pageClipboardSourceMask = pageClipboardPreview
        ? pageBit(source.structureClipboard.sequencerPage.sourcePage)
        : 0U;
    const uint16_t pageClipboardDestinationMask = pageClipboardPreview
        ? pageBit(viewedPage)
        : 0U;
    const uint16_t pageClipboardOverwriteMask =
        (pageClipboardPreview && viewedPage < sequencer.activePageCount())
            ? pageClipboardDestinationMask
            : 0U;

    const char* leftText = (selectingTrack || focusingTrack) ? "TRACKS" : "PAGES";
    std::array<char, 12> badgeText{};
    if (source.trackNavigation.selection.active.get() || sequencer.structureUi.pageSelection.active.get()) {
        std::snprintf(
            badgeText.data(),
            badgeText.size(),
            "SEL %u",
            static_cast<unsigned>(selectionCount)
        );
    } else {
        std::snprintf(
            badgeText.data(),
            badgeText.size(),
            "%s",
            clipboardBadge(source.structureClipboard)
        );
    }

    return {
        .length = sequencer.length.get(),
        .activePage = sequencer.pageForStep(sequencer.focusedStep.get()),
        .viewedPage = viewedPage,
        .previewTrack = previewTrack,
        .addPageIndex = addPageIndex,
        .enabledMask = source.sharedTrackEnabledMask.get(),
        .selectingTrack = selectingTrack,
        .selectingPage = selectingPage,
        .previewPageAddSlot = previewPageAddSlotActive,
        .pageSourceMarkerMask = static_cast<uint16_t>(
            selectingPage ? pageSelectionMask : pageClipboardSourceMask
        ),
        .pageDestinationPreviewMask = static_cast<uint16_t>(
            (selectingPage && pageDuplicateHasUsableDestination)
                ? pageDuplicatePlan.destinationMask
                : (selectingPage ? 0U : pageClipboardDestinationMask)
        ),
        .pageDestinationOverwriteMask = static_cast<uint16_t>(
            (selectingPage && pageDuplicateHasUsableDestination)
                ? pageDuplicatePlan.overwriteMask
                : (selectingPage ? 0U : pageClipboardOverwriteMask)
        ),
        .leftText = leftText,
        .badgeText = badgeText,
    };
}

SequencerBottomControlsProps buildBottomControlsProps(const SequencerViewModelSource& source) {
    const auto& sequencer = source.sequencer;

    return {
        .selectingQuickControls = sequencer.patternQuickControls.selecting.get(),
        .focusedQuickControl = sequencer.patternQuickControls.focusedItem.get(),
        .offsetSteps = sequencer.patternQuickControls.offsetSteps.get(),
        .stepsPerBeat = sequencer.stepsPerBeat.get(),
        .length = sequencer.length.get(),
    };
}

StepPropertyStripProps buildStepPropertyStripProps(const SequencerViewModelSource& source) {
    const auto& sequencer = source.sequencer;

    return {
        .activeProperty = sequencer.activeStepProperty.get(),
        .selecting = sequencer.stepPropertyInlineSelector.selecting.get(),
        .selectedIndex = sequencer.stepPropertyInlineSelector.selectedIndex.get(),
    };
}

ContextActionStripProps buildLeftActionStripProps(const SequencerViewModelSource& source) {
    const bool selectingTrack =
        source.trackNavigation.selection.active.get() &&
        source.trackNavigation.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool selectingPattern = source.sequencer.patternQuickControls.selecting.get();
    const bool selectingProperty = source.sequencer.stepPropertyInlineSelector.selecting.get();
    const bool selectingPage = source.sequencer.structureUi.pageSelection.active.get();
    const bool selectingStructure = selectingTrack || selectingPage;
    const char* propertyIcon = visual::propertyIconGlyph(source.sequencer.activeStepProperty.get());

    StripProps props;
    props.visible = true;

    if (selectingStructure) {
        props.slots[0] = makeIconSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        props.slots[1] = selectingTrack
            ? SlotProps{
                  .visualState = Visual::ACTIVE,
                  .tone = Tone::NEUTRAL,
                  .showIcon = false,
                  .icon = nullptr,
                  .showLabel = true,
                  .label = "TRK",
              }
            : SlotProps{
                  .visualState = Visual::ACTIVE,
                  .tone = Tone::NEUTRAL,
                  .showIcon = false,
                  .icon = nullptr,
                  .showLabel = true,
                  .label = "PG",
              };
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    if (selectingPattern) {
        props.slots[0] = makeIconSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        props.slots[1] = makeIconSlot(
            standalone::icons::MIDI_CHANNEL,
            Visual::ACTIVE
        );
        props.slots[2] = makeIconSlot(propertyIcon, Visual::DIM);
        return props;
    }

    if (selectingProperty) {
        props.slots[0] = makeIconSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        props.slots[1] = makeIconSlot(
            standalone::icons::MIDI_CHANNEL,
            Visual::DIM
        );
        props.slots[2] = makeIconSlot(propertyIcon, Visual::ACTIVE);
        return props;
    }

    props.slots[0].visualState = Visual::HIDDEN;
    props.slots[1] = makeIconSlot(
        standalone::icons::MIDI_CHANNEL,
        Visual::DIM
    );
    props.slots[2] = makeIconSlot(propertyIcon, Visual::DIM);
    return props;
}

ContextActionStripProps buildBottomActionStripProps(const SequencerViewModelSource& source) {
    StripProps props;
    props.visible = true;
    const bool trackFocus =
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool selectingTrack = source.trackNavigation.selection.active.get();
    const bool selectingPage = source.sequencer.structureUi.pageSelection.active.get();
    const uint16_t selectionMask = selectingTrack
        ? source.trackNavigation.selection.selectedMask.get()
        : source.sequencer.structureUi.pageSelection.selectedMask.get();

    if (selectingTrack || selectingPage) {
        bool canDeleteSelection = false;
        bool canDuplicateSelection = false;
        if (selectingTrack) {
            constexpr uint8_t TRACK_COUNT =
                core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
            const uint16_t enabledMask = source.sharedTrackEnabledMask.get();
            const uint16_t actionableMask = static_cast<uint16_t>(selectionMask & enabledMask);
            const uint8_t actionableCount = countSelectedItems(actionableMask);
            const uint8_t enabledCount = countSelectedItems(enabledMask);
            canDeleteSelection = actionableCount > 0 && actionableCount < enabledCount;
            canDuplicateSelection = actionableCount > 0 && enabledCount < TRACK_COUNT;
        } else {
            const uint8_t activePages = source.sequencer.activePageCount();
            const uint16_t actionableMask = static_cast<uint16_t>(
                selectionMask & activePageMask(activePages)
            );
            const uint8_t actionableCount = countSelectedItems(actionableMask);
            const auto plan = core::state::sequencer::buildPageDuplicatePlan(
                source.sequencer,
                actionableMask,
                source.sequencer.structureUi.pageSelection.cursorIndex.get()
            );
            canDeleteSelection = actionableCount > 0 && actionableCount < activePages;
            canDuplicateSelection = plan.hasEntries() && plan.movesAnyPage();
        }

        props.slots[0] = makeIconSlot(
            standalone::icons::ACTION_CLEAR,
            canDeleteSelection ? Visual::ACTIVE : Visual::DISABLED,
            Tone::DESTRUCTIVE
        );
        props.slots[0].showLabel = true;
        props.slots[0].label = "DEL";
        props.slots[1] = SlotProps{
            .visualState = Visual::ACTIVE,
            .tone = Tone::NEUTRAL,
            .showIcon = false,
            .icon = nullptr,
            .showLabel = true,
            .label = "SEL",
        };
        props.slots[2] = makeIconSlot(
            standalone::icons::ACTION_COPY,
            canDuplicateSelection ? Visual::ACTIVE : Visual::DISABLED,
            Tone::POSITIVE
        );
        props.slots[2].showLabel = true;
        props.slots[2].label = "DUP";
        return props;
    }

    const bool canPaste = trackFocus
        ? source.structureClipboard.hasSequencerTrack()
        : source.structureClipboard.hasSequencerPage();
    const bool previewingAddSlot = trackFocus
        ? source.trackNavigation.previewAddSlot.get()
        : source.sequencer.structureUi.previewAddPageSlot.get();
    const bool canClear = !previewingAddSlot;
    const bool pasteOverwritesDestination = canPaste && !previewingAddSlot;
    const bool copyOrPasteAvailable = canClear || canPaste;
    const auto& holdState = trackFocus ? source.trackNavigation.hold : source.sequencer.structureUi.pageHold;
    const auto holdAction = holdState.action.get();
    const bool removeHoldActive = holdAction == core::state::StructureHoldAction::REMOVE;
    const bool pasteHoldActive = holdAction == core::state::StructureHoldAction::PASTE;

    props.slots[0] = makeIconSlot(
        standalone::icons::ACTION_CLEAR,
        removeHoldActive ? Visual::ARMED : (canClear ? Visual::ACTIVE : Visual::DISABLED),
        removeHoldActive ? Tone::DESTRUCTIVE : Tone::WARNING
    );
    props.slots[0].showLabel = canClear;
    props.slots[0].label = removeHoldActive ? "DEL" : (canClear ? "CLR" : nullptr);
    props.slots[0].holdActive = removeHoldActive;
    props.slots[0].holdStartedAtMs = holdState.startedAtMs.get();
    props.slots[0].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    props.slots[1] = SlotProps{
        .visualState = Visual::DIM,
        .tone = Tone::NEUTRAL,
        .showIcon = false,
        .icon = nullptr,
        .showLabel = true,
        .label = trackFocus ? "TRK" : "PG",
    };
    props.slots[2] = makeIconSlot(
        standalone::icons::ACTION_COPY,
        pasteHoldActive && canPaste
            ? Visual::ARMED
            : (copyOrPasteAvailable ? Visual::ACTIVE : Visual::DISABLED),
        pasteHoldActive && canPaste
            ? (pasteOverwritesDestination ? Tone::WARNING : Tone::POSITIVE)
            : Tone::NEUTRAL
    );
    props.slots[2].showLabel = pasteHoldActive && canPaste;
    props.slots[2].label = pasteHoldActive && canPaste ? "PST" : nullptr;
    props.slots[2].holdActive = pasteHoldActive;
    props.slots[2].holdStartedAtMs = holdState.startedAtMs.get();
    props.slots[2].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    return props;
}

grid::StepGridFrameState buildStepGridProps(const SequencerViewModelSource& source) {
    return grid::buildStepGridFrameState(source.sequencer);
}

}  // namespace core::ui::sequencer

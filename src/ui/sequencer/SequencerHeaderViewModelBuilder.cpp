#include "ui/sequencer/SequencerHeaderViewModelBuilder.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/StructureClipboardPastePlan.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"

namespace core::ui::sequencer {

namespace {

const char* clipboardBadge(const core::state::StructureClipboardState& clipboard) {
    switch (clipboard.kind.get()) {
        case core::state::StructureClipboardKind::SEQUENCER_PAGE:
        case core::state::StructureClipboardKind::SEQUENCER_PAGE_SELECTION:
            return "Pattern";
        case core::state::StructureClipboardKind::SEQUENCER_TRACK:
        case core::state::StructureClipboardKind::SEQUENCER_TRACK_SELECTION:
            return "Track";
        case core::state::StructureClipboardKind::SEQUENCER_STEP_CONTENT:
            return "Step";
        case core::state::StructureClipboardKind::SEQUENCER_STEPS:
            return "Steps";
        default:
            return "";
    }
}

uint16_t pageBit(uint8_t page) {
    if (page >= core::state::sequencer::SequencerState::PAGE_COUNT) return 0;
    return static_cast<uint16_t>(1U << page);
}

}  // namespace

FLASHMEM SequencerHeaderBarProps buildSequencerHeaderBarProps(
    const SequencerViewModelSource& source
) {
    const auto& sequencer = source.sequencer;
    const uint8_t activeTrack = source.sharedTrackActive.get();
    const bool focusingTrack =
        !source.trackNavigation.selection.active.get() &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool focusingStep =
        !source.trackNavigation.selection.active.get() &&
        !sequencer.structureUi.pageSelection.active.get() &&
        !sequencer.structureUi.stepSelection.active.get() &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::STEP;
    const bool selectingTrack =
        source.trackNavigation.selection.active.get() &&
        source.trackNavigation.selection.scope.get() ==
            core::state::StructureSelectionScope::TRACK;
    const bool selectingPage =
        sequencer.structureUi.pageSelection.active.get() &&
        sequencer.structureUi.pageSelection.scope.get() ==
            core::state::StructureSelectionScope::PAGE;
    const bool selectingStep = sequencer.structureUi.stepSelection.active.get();
    const uint16_t pageSelectionMask = sequencer.structureUi.pageSelection.selectedMask.get();
    const bool previewAddTrackSlot =
        !source.trackNavigation.selection.active.get() &&
        source.trackNavigation.previewAddSlot.get();
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
        selectingStep
            ? std::min<uint8_t>(
                  sequencer.page.get(),
                  static_cast<uint8_t>(
                      core::state::sequencer::SequencerState::PAGE_COUNT - 1U
                  )
              )
        : selectingPage
            ? sequencer.structureUi.pageSelection.cursorIndex.get()
            : ((previewAddPageSlot &&
                addPageIndex < core::state::sequencer::SequencerState::PAGE_COUNT)
                   ? addPageIndex
                   : sequencer.visiblePage());
    const bool previewPageAddSlotActive =
        previewAddPageSlot &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE &&
        addPageIndex < core::state::sequencer::SequencerState::PAGE_COUNT;
    const auto pageSelectionPastePlan =
        (selectingPage && source.structureClipboard.hasSequencerPageSelection())
            ? core::state::buildSequencerPageSelectionPastePlan(
                  source.structureClipboard.sequencerPageSelection,
                  viewedPage,
                  sequencer.activePageCount()
              )
            : core::state::SequencerPageSelectionPastePlan{};
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

    const bool microContext = core::state::sequencer::isMicroSequenceContentView(sequencer);
    const bool cycleContext = core::state::sequencer::isCycleStatesContentView(sequencer);
    const char* leftText = microContext
        ? "Micro"
        : (cycleContext
               ? "Cycle"
               : ((selectingStep || focusingStep)
                      ? "Step"
                      : ((selectingTrack || focusingTrack) ? "Track" : "Pattern")));
    std::array<char, 12> badgeText{};
    if (!source.trackNavigation.selection.active.get() &&
        !sequencer.structureUi.pageSelection.active.get() &&
        !sequencer.structureUi.stepSelection.active.get()) {
        const char* badge = clipboardBadge(source.structureClipboard);
        if (badge[0] != '\0' && std::strcmp(badge, leftText) == 0) {
            badge = "Copied";
        }
        std::snprintf(
            badgeText.data(),
            badgeText.size(),
            "%s",
            badge
        );
    }

    return {
        .length = core::state::sequencer::activeContentLength(sequencer),
        .activePage =
            core::state::sequencer::activeContentPageForStep(sequencer.focusedStep.get()),
        .viewedPage = viewedPage,
        .previewTrack = previewTrack,
        .addPageIndex = addPageIndex,
        .enabledMask = source.sharedTrackEnabledMask.get(),
        .selectingTrack = selectingTrack,
        .selectingPage = selectingPage,
        .selectingStep = selectingStep,
        .previewPageAddSlot = previewPageAddSlotActive,
        .pageSourceMarkerMask = static_cast<uint16_t>(
            selectingPage ? pageSelectionMask : pageClipboardSourceMask
        ),
        .pageDestinationPreviewMask = static_cast<uint16_t>(
            selectingPage
                ? pageSelectionPastePlan.destinationMask
                : (selectingPage ? 0U : pageClipboardDestinationMask)
        ),
        .pageDestinationOverwriteMask = static_cast<uint16_t>(
            selectingPage
                ? pageSelectionPastePlan.overwriteMask
                : (selectingPage ? 0U : pageClipboardOverwriteMask)
        ),
        .leftText = leftText,
        .badgeText = badgeText,
    };
}

}  // namespace core::ui::sequencer

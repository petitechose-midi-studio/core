#include "ui/sequencer/SequencerHeaderViewModelBuilder.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"

namespace core::ui::sequencer {

namespace {

const char* clipboardBadge(const core::state::StructureClipboardState& clipboard) {
    switch (clipboard.kind.get()) {
        case core::state::StructureClipboardKind::SEQUENCER_PAGE:
            return "Page";
        case core::state::StructureClipboardKind::SEQUENCER_TRACK:
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
    const bool selectingTrack =
        source.trackNavigation.selection.active.get() &&
        source.trackNavigation.selection.scope.get() ==
            core::state::StructureSelectionScope::TRACK;
    const bool selectingPage =
        sequencer.structureUi.pageSelection.active.get() &&
        sequencer.structureUi.pageSelection.scope.get() ==
            core::state::StructureSelectionScope::PAGE;
    const bool selectingStep = sequencer.structureUi.stepSelection.active.get();
    const bool anySelection =
        selectingTrack || selectingPage || selectingStep;
    const bool focusingTrack =
        !anySelection &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool focusingStep =
        !anySelection &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::STEP;
    const auto& trackPaste = sequencer.structureUi.trackPaste;
    const bool trackPasteDetailsAvailable =
        focusingTrack && trackPaste.inspectable() &&
        trackPaste.plan.canCommit() && trackPaste.feedback.active;
    const bool previewAddTrackSlot =
        !selectingTrack && source.trackNavigation.previewAddSlot.get();
    const uint8_t addTrackIndex =
        (previewAddTrackSlot &&
         source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK)
            ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
                  source.trackNavigation.previewTrackIndex.get()
              )
            : core::ui::SequencerHeaderBarProps::TRACK_COUNT;
    const uint8_t previewTrack = selectingTrack
        ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
              source.trackNavigation.selection.cursorIndex.get()
          )
        : ((previewAddTrackSlot &&
         addTrackIndex < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT)
            ? addTrackIndex
            : activeTrack);
    const uint8_t viewedPage =
        selectingStep
            ? std::min<uint8_t>(
                  sequencer.page.get(),
                  static_cast<uint8_t>(
                      core::state::sequencer::SequencerState::PAGE_COUNT - 1U
                  )
              )
        : selectingPage
            ? std::min<uint8_t>(
                  sequencer.structureUi.pageSelection.cursorIndex.get(),
                  static_cast<uint8_t>(
                      core::state::sequencer::SequencerState::PAGE_COUNT - 1U
                  )
              )
        : sequencer.visiblePage();
    const bool pageClipboardPreview =
        !anySelection &&
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
    const bool pageSelectionPlacing =
        selectingPage &&
        sequencer.structureUi.pageSelection.placing.get();
    const uint16_t pageSelectionDestinationMask =
        pageSelectionPlacing
            ? sequencer.structureUi.pageSelection.destinationMask.get()
            : 0U;
    const uint16_t pageSelectionOverwriteMask =
        pageSelectionPlacing
            ? sequencer.structureUi.pageSelection.overwriteMask.get()
            : 0U;
    const uint16_t pageSelectionBlockedMask =
        pageSelectionPlacing &&
        sequencer.structureUi.pageSelection.pasteBlocked.get()
            ? pageSelectionDestinationMask
            : 0U;

    const bool microContext = core::state::sequencer::isMicroSequenceContentView(sequencer);
    const bool cycleContext = core::state::sequencer::isCycleStatesContentView(sequencer);
    const bool ccLaneGrid = sequencer.ccLaneUi.mode ==
        core::state::sequencer::SequencerCcLaneUiMode::LANE_GRID;
    const char* leftText = ccLaneGrid
        ? "CC lane"
        : microContext
        ? "Micro"
        : (cycleContext
               ? "Cycle"
               : ((selectingStep || focusingStep)
                      ? "Step"
                       : ((selectingTrack || focusingTrack)
                              ? "Track"
                              : "Pattern")));
    std::array<char, 12> badgeText{};
    if (ccLaneGrid) {
        const auto* bank =
            core::state::sequencer::sequencerCcLaneView(
                core::state::sequencer::authoringPattern(sequencer)
            );
        if (bank != nullptr && sequencer.ccLaneUi.focusedLane < bank->lanes.size() &&
            bank->lanes[sequencer.ccLaneUi.focusedLane].occupied) {
            std::snprintf(
                badgeText.data(),
                badgeText.size(),
                "%u",
                static_cast<unsigned>(
                    bank->lanes[sequencer.ccLaneUi.focusedLane]
                        .destination.controller
                )
            );
        }
    } else if (trackPasteDetailsAvailable) {
        std::snprintf(
            badgeText.data(),
            badgeText.size(),
            "%s",
            trackPaste.detailVisible ? "LC Close" : "LC Details"
        );
    } else if (!anySelection) {
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
        .enabledMask = source.sharedTrackEnabledMask.get(),
        .selectingTrack = selectingTrack,
        .selectingPage = selectingPage,
        .selectingStep = selectingStep,
        .pageSourceMarkerMask = selectingPage
            ? static_cast<uint16_t>(
                  sequencer.structureUi.pageSelection.selectedMask.get() &
                  core::state::shared::prefixMask(
                      core::state::sequencer::activeContentPageCount(sequencer)
                  )
              )
            : pageClipboardSourceMask,
        .pageDestinationPreviewMask = pageSelectionPlacing
            ? pageSelectionDestinationMask
            : pageClipboardDestinationMask,
        .pageDestinationOverwriteMask = pageSelectionPlacing
            ? pageSelectionOverwriteMask
            : pageClipboardOverwriteMask,
        .pageDestinationBlockedMask = pageSelectionBlockedMask,
        .leftText = leftText,
        .badgeText = badgeText,
    };
}

}  // namespace core::ui::sequencer

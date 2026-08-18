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
#include "state/sequencer/SequencerNoteSpelling.hpp"
#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

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
        case core::state::StructureClipboardKind::SEQUENCER_DRUM_LANE_SELECTION:
            return "Lanes";
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
    const auto& drumUi = sequencer.drumSequencer;
    const bool drumGrid =
        core::state::sequencer::isDrumOverviewActive(sequencer);
    const bool drumChild =
        core::state::sequencer::isDrumContentView(sequencer);
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
    const bool selectingDrumLanes =
        drumGrid && drumUi.laneSelection.active;
    const bool anySelection =
        selectingTrack || selectingPage || selectingStep ||
        selectingDrumLanes;
    const bool previewEmptyTrack =
        !anySelection && sequencerPreviewingEmptyTrack(source);
    const bool focusingTrack =
        !anySelection &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool drumGridContext =
        drumGrid && !selectingTrack && !previewEmptyTrack;
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
    const uint8_t viewedPage = previewEmptyTrack
        ? 0U
        : drumGridContext
        ? std::min<uint8_t>(
              drumUi.page,
              static_cast<uint8_t>(
                  core::state::sequencer::SequencerState::PAGE_COUNT - 1U
              )
          )
        : selectingStep
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
    const bool pageStripVisible =
        selectingPage || pageClipboardPreview ||
        (!anySelection &&
         source.navigationFocus.get() ==
             core::state::StructureNavigationFocus::PAGE);
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
    const char* leftText = drumGrid
        ? (selectingDrumLanes
               ? "Lanes"
               : (selectingStep || focusingStep)
               ? "Step"
               : ((selectingTrack || focusingTrack) ? "Track" : "Pattern"))
        : ccLaneGrid
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
    std::array<char, 20> badgeText{};
    std::array<core::ui::SequencerHeaderMetricProps, 2> metrics{};
    const char* contextIcon = "";
    uint32_t contextIconColor = 0U;
    std::array<char, 8> pageText{};
    uint8_t headerLength =
        core::state::sequencer::activeContentLength(sequencer);
    uint8_t headerActivePage =
        core::state::sequencer::activeContentPageForStep(
            sequencer.focusedStep.get()
        );
    if (drumGridContext && drumUi.drumTrack != nullptr) {
        const uint8_t laneCount = std::min<uint8_t>(
            drumUi.drumTrack->kit.laneCount,
            core::state::sequencer::DRUM_MAX_LANES
        );
        if (laneCount > 0U) {
            const uint8_t lane = std::min<uint8_t>(
                drumUi.selectedLane,
                static_cast<uint8_t>(laneCount - 1U)
            );
            const auto& lanePattern = drumUi.drumTrack->pattern.lanes[lane];
            headerLength = drumUi.drumTrack->pattern.effectiveLength(lane);
            const uint8_t stepsPerBeat =
                drumUi.drumTrack->pattern.effectiveStepsPerBeat(lane);
            // Length/division describe the focused Lane; pagination describes
            // the complete polymetric Pattern shared by every Lane row.
            const uint8_t pageCount = drumUi.overviewPageCount();
            headerActivePage = std::min<uint8_t>(
                drumUi.page,
                static_cast<uint8_t>(pageCount - 1U)
            );
            if (!drumUi.laneAddSlotFocused()) {
                std::snprintf(
                    metrics[0].value.data(),
                    metrics[0].value.size(),
                    "%u",
                    static_cast<unsigned>(headerLength)
                );
                metrics[0].icon = standalone::icons::LENGTH;
                std::snprintf(
                    metrics[1].value.data(),
                    metrics[1].value.size(),
                    "1/%u%s",
                    static_cast<unsigned>(stepsPerBeat * 4U),
                    lanePattern.timing.mode == core::state::sequencer::
                            DrumLaneTimingMode::CUSTOM
                        ? "*"
                        : ""
                );
                metrics[1].icon = standalone::icons::DIVISION;
                const auto propertyVisual =
                    visual::buildDrumPropertyVisual(drumUi.property);
                contextIcon = propertyVisual.icon;
                contextIconColor = propertyVisual.color;
                std::snprintf(
                    pageText.data(),
                    pageText.size(),
                    "%u/%u",
                    static_cast<unsigned>(headerActivePage + 1U),
                    static_cast<unsigned>(pageCount)
                );
            }
        }
        if (selectingDrumLanes) {
            const uint16_t mask = drumUi.laneSelection.selectedMask;
            uint8_t count = 0U;
            for (uint8_t lane = 0U;
                 lane < core::state::sequencer::DRUM_MAX_LANES;
                 ++lane) {
                if ((mask & static_cast<uint16_t>(1U << lane)) != 0U) {
                    ++count;
                }
            }
            std::snprintf(
                badgeText.data(),
                badgeText.size(),
                drumUi.laneSelection.moveActive()
                    ? "Move %u"
                    : drumUi.laneSelection.placementActive()
                        ? "Place %u"
                        : "%u selected",
                static_cast<unsigned>(count)
            );
            contextIcon = "";
            contextIconColor = 0U;
            pageText.fill('\0');
        } else if (drumUi.laneAddSlotFocused()) {
            std::snprintf(
                badgeText.data(), badgeText.size(), "%s", "Add lane"
            );
        } else if (laneCount == 0U) {
            std::snprintf(
                badgeText.data(), badgeText.size(), "%s", "No lanes"
            );
        }
    } else if (drumChild && drumUi.drumTrack != nullptr &&
               sequencer.contentView.drumOwnerLane <
                   drumUi.drumTrack->kit.laneCount &&
               sequencer.contentView.drumOwnerLane <
                   core::state::sequencer::DRUM_MAX_LANES) {
        std::snprintf(
            badgeText.data(),
            badgeText.size(),
            "%s",
            core::state::sequencer::drumLaneDisplayName(
                drumUi.drumTrack->kit.lanes[
                    sequencer.contentView.drumOwnerLane]
            )
        );
    } else if (ccLaneGrid) {
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
    } else if (focusingStep) {
        const auto displayContext =
            core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
                sequencer,
                source.tracks.projectScaleSettings(),
                sequencer.activeStepProperty.get()
            );
        const auto focused =
            core::state::sequencer::buildSequencerResolvedStepDisplayState(
                displayContext,
                sequencer.focusedStep.get(),
                false
            );
        if (focused.valid) {
            uint8_t note = focused.note;
            if (focused.variation.visible &&
                focused.variation.deltaVisible) {
                note = focused.variation.resolved.resolved.note;
            }
            core::state::sequencer::note_spelling::formatTonalNoteLabel(
                badgeText.data(),
                badgeText.size(),
                note,
                displayContext.scaleSettings
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

    if (!previewEmptyTrack && pageText[0] == '\0' && !pageStripVisible) {
        const uint8_t pageCount = std::max<uint8_t>(
            1U,
            core::state::sequencer::activeContentPageCount(sequencer)
        );
        std::snprintf(
            pageText.data(),
            pageText.size(),
            "%u/%u",
            static_cast<unsigned>(std::min<uint8_t>(
                headerActivePage,
                static_cast<uint8_t>(pageCount - 1U)
            ) + 1U),
            static_cast<unsigned>(pageCount)
        );
    }

    return {
        .length = previewEmptyTrack
            ? static_cast<uint8_t>(0U)
            : headerLength,
        .activePage = previewEmptyTrack
            ? static_cast<uint8_t>(0U)
            : headerActivePage,
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
        .pageStripVisible = pageStripVisible,
        .leftText = leftText,
        .badgeText = badgeText,
        .metrics = metrics,
        .contextIcon = contextIcon,
        .contextIconColor = contextIconColor,
        .pageText = pageText,
    };
}

}  // namespace core::ui::sequencer

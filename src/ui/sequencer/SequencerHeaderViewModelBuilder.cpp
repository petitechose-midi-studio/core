#include "ui/sequencer/SequencerHeaderViewModelBuilder.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectTrackDomainOps.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerCcLanePatternOps.hpp"
#include "state/sequencer/SequencerNoteSpelling.hpp"
#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "state/sequencer/SequencerStepContentDraftOps.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/SequencerQuickControlVisuals.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

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

template <size_t Size>
void copyText(std::array<char, Size>& destination, const char* source) {
    static_assert(Size > 0U);
    destination.fill('\0');
    if (source == nullptr) return;
    size_t length = 0U;
    while (length + 1U < Size && source[length] != '\0') ++length;
    std::memcpy(destination.data(), source, length);
}

FLASHMEM bool inlinePitchFeedbackStep(
    const core::state::sequencer::SequencerState& sequencer,
    uint8_t& step
) {
    if (!sequencer.stepInlineFeedback.visible.get() ||
        sequencer.stepInlineFeedback.property.get() !=
            core::state::sequencer::StepProperty::NOTE) {
        return false;
    }

    const auto mask = sequencer.stepInlineFeedback.touchedMask.get();
    step = sequencer.focusedStep.get();
    if (mask.test(step)) return true;

    for (uint16_t candidate = 0U;
         candidate < core::state::sequencer::SequencerState::MAX_STEPS;
         ++candidate) {
        if (!mask.test(static_cast<uint8_t>(candidate))) continue;
        step = static_cast<uint8_t>(candidate);
        return true;
    }
    return false;
}

}  // namespace

FLASHMEM SequencerHeaderBarProps buildSequencerHeaderBarProps(
    const SequencerViewModelSource& source
) {
    const auto& sequencer = source.sequencer;
    if (sequencer.clipWorkspace.matrixVisible()) {
        const auto& launcher = sequencer.clipWorkspace;
        const bool selectingTrack =
            source.trackNavigation.selection.active.get() &&
            source.trackNavigation.selection.scope.get() ==
                core::state::StructureSelectionScope::TRACK;
        const uint8_t focusedTrack = selectingTrack
            ? source.trackNavigation.selection.cursorIndex.get()
            : launcher.focusedTrack;
        SequencerHeaderBarProps props{};
        props.previewTrack = focusedTrack;
        props.enabledMask = source.sharedTrackEnabledMask.get();
        props.pageStripVisible = false;
        props.previewLayout = true;
        props.leftText = launcher.editor == core::state::sequencer::
                ClipWorkspaceEditor::SLOT_ACTION
            ? "Slot"
            : launcher.editor == core::state::sequencer::
                    ClipWorkspaceEditor::CLIP_BEHAVIOR
                ? "Clip"
                : launcher.editor == core::state::sequencer::
                        ClipWorkspaceEditor::SCENE_BEHAVIOR
                    ? "Scene"
                    : selectingTrack
                        ? source.trackNavigation.selection.placementActive()
                            ? "Paste"
                            : "Tracks"
                        : launcher.operation == core::state::sequencer::
                                ClipWorkspaceOperation::MOVE_DESTINATION
                            ? "Move"
                            : launcher.operation == core::state::sequencer::
                                    ClipWorkspaceOperation::
                                        DUPLICATE_DESTINATION
                                ? "Copy"
                                : launcher.operation ==
                                          core::state::sequencer::
                                              ClipWorkspaceOperation::SELECT
                                    ? "Select"
                                    : launcher.trackHeaderFocused()
                                        ? "Track"
                                        : launcher.sceneFocused()
                                            ? "Scene"
                                            : "Clip";
        if (selectingTrack) {
            const uint8_t selected = core::state::shared::countEnabled(
                static_cast<uint16_t>(
                    source.trackNavigation.selection.selectedMask.get() &
                    source.sharedTrackEnabledMask.get()
                ),
                core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
            );
            if (selected > 0U) {
                std::snprintf(
                    props.badgeText.data(), props.badgeText.size(),
                    "%u selected", static_cast<unsigned>(selected)
                );
            } else {
                core::state::project::formatProjectTrackName(
                    source.projectTracks,
                    focusedTrack,
                    props.badgeText.data(),
                    props.badgeText.size()
                );
            }
        } else if (launcher.editorActive()) {
            if (launcher.editor == core::state::sequencer::
                    ClipWorkspaceEditor::SCENE_BEHAVIOR) {
                std::snprintf(
                    props.badgeText.data(), props.badgeText.size(),
                    "S%u",
                    static_cast<unsigned>(launcher.focusedSlot + 1U)
                );
            } else {
                core::state::project::formatProjectTrackName(
                    source.projectTracks,
                    launcher.focusedTrack,
                    props.badgeText.data(),
                    props.badgeText.size()
                );
            }
        } else if (launcher.feedback == core::state::sequencer::
                ClipWorkspaceFeedback::FAILED) {
            std::snprintf(
                props.badgeText.data(), props.badgeText.size(), "%s", "Unavailable"
            );
        } else if (launcher.feedback == core::state::sequencer::
                       ClipWorkspaceFeedback::MOVED) {
            std::snprintf(
                props.badgeText.data(), props.badgeText.size(), "%s", "Moved"
            );
        } else if (launcher.feedback == core::state::sequencer::
                       ClipWorkspaceFeedback::DUPLICATED) {
            std::snprintf(
                props.badgeText.data(), props.badgeText.size(), "%s", "Duplicated"
            );
        } else if (launcher.feedback == core::state::sequencer::
                       ClipWorkspaceFeedback::REMOVED) {
            std::snprintf(
                props.badgeText.data(), props.badgeText.size(), "%s", "Removed"
            );
        } else if (launcher.placementActive()) {
            std::snprintf(
                props.badgeText.data(),
                props.badgeText.size(),
                "C%u > C%u",
                static_cast<unsigned>(launcher.sourceSlot + 1U),
                static_cast<unsigned>(launcher.focusedSlot + 1U)
            );
        } else if (launcher.trackHeaderFocused()) {
            const bool enabled =
                (source.sharedTrackEnabledMask.get() &
                 static_cast<uint16_t>(1U << launcher.focusedTrack)) != 0U;
            if (enabled) {
                core::state::project::formatProjectTrackName(
                    source.projectTracks,
                    launcher.focusedTrack,
                    props.badgeText.data(),
                    props.badgeText.size()
                );
            } else {
                copyText(props.badgeText, "Add track");
            }
        } else if (launcher.sceneFocused()) {
            const bool used = source.clips.sceneUsed(launcher.focusedSlot);
            if (used) {
                std::snprintf(
                    props.badgeText.data(), props.badgeText.size(),
                    "Scene %u",
                    static_cast<unsigned>(launcher.focusedSlot + 1U)
                );
            } else {
                copyText(props.badgeText, "Add scene");
            }
        } else {
            core::state::project::formatProjectTrackName(
                source.projectTracks,
                launcher.focusedTrack,
                props.badgeText.data(),
                props.badgeText.size()
            );
        }
        const bool trackContext =
            selectingTrack || launcher.trackHeaderFocused();
        if (trackContext) {
            props.contextIcon = source.tracks.isDrumTrack(focusedTrack)
                ? standalone::icons::DRUM_GENERIC
                : standalone::icons::NOTE;
            props.contextIconColor =
                standalone::theme::color::trackColor(focusedTrack);
        } else if (launcher.quickPropertyArmed) {
            props.contextIcon = visual::launcherQuickActionIconGlyph(
                launcher.quickAction
            );
            props.contextIconColor = standalone::theme::color::STEP_STATE;
        }
        return props;
    }
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
    const bool focusingLane =
        !anySelection &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::LANE;
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
               : focusingLane
               ? "Lane"
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
    if (sequencer.clipWorkspace.patternVisible()) {
        const uint8_t editedTrack = std::min<uint8_t>(
            sequencer.clipWorkspace.returnTrack,
            static_cast<uint8_t>(
                core::state::sequencer::SequencerTrackBankState::TRACK_COUNT - 1U
            )
        );
        const uint8_t editedSlot = std::min<uint8_t>(
            sequencer.clipWorkspace.returnSlot,
            static_cast<uint8_t>(
                core::state::sequencer::SequencerClipGridState::SLOT_COUNT - 1U
            )
        );
        std::snprintf(
            badgeText.data(), badgeText.size(),
            "T%u / C%u",
            static_cast<unsigned>(editedTrack + 1U),
            static_cast<unsigned>(editedSlot + 1U)
        );
    }
    std::array<core::ui::SequencerHeaderMetricProps, 2> metrics{};
    const char* contextIcon = "";
    uint32_t contextIconColor = 0U;
    std::array<char, SequencerHeaderBarProps::PAGE_TEXT_SIZE> pageText{};
    uint8_t pitchFeedbackStep = 0U;
    const bool inlinePitchFeedback =
        !anySelection && !previewEmptyTrack && !drumGrid &&
        inlinePitchFeedbackStep(sequencer, pitchFeedbackStep);
    std::array<char, SequencerHeaderBarProps::PAGE_TEXT_SIZE> tonalPitchText{};
    if (!drumGrid && !drumChild && !ccLaneGrid &&
        (focusingStep || inlinePitchFeedback)) {
        const auto displayContext =
            core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
                sequencer,
                source.tracks.projectScaleSettings(),
                inlinePitchFeedback
                    ? core::state::sequencer::StepProperty::NOTE
                    : sequencer.activeStepProperty.get()
            );
        const auto display =
            core::state::sequencer::buildSequencerResolvedStepDisplayState(
                displayContext,
                inlinePitchFeedback
                    ? pitchFeedbackStep
                    : sequencer.focusedStep.get(),
                inlinePitchFeedback
            );
        if (display.valid) {
            uint8_t note = display.note;
            if (!inlinePitchFeedback && display.variation.visible &&
                display.variation.deltaVisible) {
                note = display.variation.resolved.resolved.note;
            }
            core::state::sequencer::note_spelling::formatTonalNoteLabel(
                tonalPitchText.data(),
                tonalPitchText.size(),
                note,
                displayContext.scaleSettings
            );
        }
    }
    uint8_t headerLength =
        core::state::sequencer::activeContentLength(sequencer);
    uint8_t headerActivePage =
        core::state::sequencer::activeContentPageForStep(
            sequencer.focusedStep.get()
        );
    if (drumGridContext && drumUi.drumTrack() != nullptr) {
        const uint8_t laneCount = std::min<uint8_t>(
            drumUi.drumTrack()->kit.laneCount,
            core::state::sequencer::DRUM_MAX_LANES
        );
        if (laneCount > 0U) {
            const uint8_t lane = std::min<uint8_t>(
                drumUi.selectedLane,
                static_cast<uint8_t>(laneCount - 1U)
            );
            const auto& pattern = drumUi.drumTrack()->pattern;
            const auto& lanePattern = pattern.lanes[lane];
            const bool laneMetrics = focusingLane || focusingStep ||
                selectingDrumLanes || selectingStep;
            headerLength = laneMetrics
                ? pattern.effectiveLength(lane)
                : pattern.defaultLength;
            const uint8_t stepsPerBeat = laneMetrics
                ? pattern.effectiveStepsPerBeat(lane)
                : pattern.defaultStepsPerBeat;
            // Metrics follow their semantic owner; pagination always describes
            // the complete polymetric Pattern shared by every Lane row.
            const uint8_t pageCount = drumUi.overviewPageCount();
            headerActivePage = std::min<uint8_t>(
                drumUi.page,
                static_cast<uint8_t>(pageCount - 1U)
            );
            if (!drumUi.laneAddSlotFocused() || !laneMetrics) {
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
                    laneMetrics && lanePattern.timing.mode == core::state::sequencer::
                            DrumLaneTimingMode::CUSTOM
                        ? "*"
                        : ""
                );
                metrics[1].icon = standalone::icons::DIVISION;
                if (focusingLane || focusingStep) {
                    const auto propertyVisual =
                        visual::buildDrumPropertyVisual(drumUi.property);
                    contextIcon = propertyVisual.icon;
                    contextIconColor = propertyVisual.color;
                }
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
        } else if (focusingLane && drumUi.laneAddSlotFocused()) {
            std::snprintf(
                badgeText.data(), badgeText.size(), "%s", "Add lane"
            );
        } else if (laneCount == 0U) {
            std::snprintf(
                badgeText.data(), badgeText.size(), "%s", "No lanes"
            );
        }
    } else if (drumChild && drumUi.drumTrack() != nullptr &&
               sequencer.contentView.drumOwnerLane <
                   drumUi.drumTrack()->kit.laneCount &&
               sequencer.contentView.drumOwnerLane <
                   core::state::sequencer::DRUM_MAX_LANES) {
        copyText(
            badgeText,
            core::state::sequencer::drumLaneDisplayName(
                drumUi.drumTrack()->kit.lanes[
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
        if (tonalPitchText[0] != '\0') {
            copyText(badgeText, tonalPitchText.data());
        }
    } else if (trackPasteDetailsAvailable) {
        copyText(
            badgeText,
            trackPaste.detailVisible ? "LC Close" : "LC Details"
        );
    } else if (!anySelection) {
        const char* badge = clipboardBadge(source.structureClipboard);
        if (badge[0] != '\0') {
            if (std::strcmp(badge, leftText) == 0) badge = "Copied";
            copyText(badgeText, badge);
        }
    }

    if (inlinePitchFeedback && tonalPitchText[0] != '\0') {
        pageText = tonalPitchText;
        contextIcon = visual::propertyIconGlyph(
            core::state::sequencer::StepProperty::NOTE
        );
        contextIconColor = semantic::colorForProperty(
            core::state::sequencer::StepProperty::NOTE
        );
        if (focusingStep) badgeText.fill('\0');
    }

    if (sequencer.patternPresetPreview.active()) {
        const bool queued = sequencer.patternPresetPreview.queued();
        copyText(badgeText, sequencer.patternPresetPreview.name.data());
        std::snprintf(
            pageText.data(),
            pageText.size(),
            "%s",
            queued ? "Next loop" : "Preview"
        );
        contextIcon = queued
            ? standalone::icons::STATUS_QUEUED
            : standalone::icons::STATUS_PREVIEW;
        contextIconColor = queued
            ? standalone::theme::color::ACTIVE
            : standalone::theme::color::LIVE_TIME;
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
        .pageStripVisible = pageStripVisible &&
            !sequencer.patternPresetPreview.active(),
        .previewLayout = sequencer.patternPresetPreview.active(),
        .leftText = leftText,
        .badgeText = badgeText,
        .metrics = metrics,
        .contextIcon = contextIcon,
        .contextIconColor = contextIconColor,
        .pageText = pageText,
    };
}

}  // namespace core::ui::sequencer

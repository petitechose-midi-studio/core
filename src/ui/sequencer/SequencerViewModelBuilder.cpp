#include "ui/sequencer/SequencerViewModelBuilder.hpp"

#include <oc/type/TextFormat.hpp>

#include "config/Timing.hpp"
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

}  // namespace

SequencerHeaderBarProps buildHeaderBarProps(const SequencerViewModelSource& source) {
    const auto& sequencer = source.sequencer;
    const auto& status = source.statusBar;
    const uint8_t activeTrack = source.sharedTrackActive.get();
    const bool focusingTrack =
        !sequencer.structureUi.selection.active.get() &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool focusingPage =
        !sequencer.structureUi.selection.active.get() &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE;
    const bool selectingTrack =
        sequencer.structureUi.selection.active.get() &&
        sequencer.structureUi.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool selectingPage =
        sequencer.structureUi.selection.active.get() &&
        sequencer.structureUi.selection.scope.get() == core::state::StructureSelectionScope::PAGE;
    const uint16_t selectedMask = sequencer.structureUi.selection.selectedMask.get();
    const uint8_t selectionCount = countSelectedItems(selectedMask);
    const bool previewAddSlot =
        !sequencer.structureUi.selection.active.get() && sequencer.structureUi.previewAddSlot.get();
    const uint8_t addTrackIndex =
        (previewAddSlot &&
         source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK)
            ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
                  sequencer.structureUi.previewTrackIndex.get()
              )
            : core::ui::SequencerHeaderBarProps::TRACK_COUNT;
    const uint8_t previewTrack =
        selectingTrack
            ? sequencer.structureUi.selection.cursorIndex.get()
            : ((previewAddSlot && addTrackIndex < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT)
                   ? addTrackIndex
                   : activeTrack);
    const uint8_t addPageIndex =
        (previewAddSlot &&
         source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE)
            ? sequencer.clampPage(sequencer.structureUi.previewPageIndex.get())
            : core::state::sequencer::SequencerState::PAGE_COUNT;
    const uint8_t viewedPage =
        selectingPage
            ? sequencer.structureUi.selection.cursorIndex.get()
            : ((previewAddSlot && addPageIndex < core::state::sequencer::SequencerState::PAGE_COUNT)
                   ? addPageIndex
                   : sequencer.visiblePage());
    const bool previewTrackAddSlot =
        previewAddSlot &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK &&
        addTrackIndex < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT;
    const bool previewPageAddSlot =
        previewAddSlot &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE &&
        addPageIndex < core::state::sequencer::SequencerState::PAGE_COUNT;

    const char* leftText = (selectingTrack || focusingTrack) ? "TRACKS" : "PAGES";
    std::array<char, 12> badgeText{};
    if (sequencer.structureUi.selection.active.get()) {
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

    std::array<uint8_t, SequencerHeaderBarProps::TRACK_COUNT> trackActivity{};
    for (uint8_t i = 0; i < trackActivity.size(); ++i) {
        trackActivity[i] = status.trackNoteActivity[i].get();
    }

    return {
        .length = sequencer.length.get(),
        .activePage = sequencer.pageForStep(sequencer.focusedStep.get()),
        .viewedPage = viewedPage,
        .playheadStep = sequencer.playheadStep.get(),
        .activeTrack = activeTrack,
        .previewTrack = previewTrack,
        .addPageIndex = addPageIndex,
        .addTrackIndex = addTrackIndex,
        .enabledMask = source.sharedTrackEnabledMask.get(),
        .focusingTrack = focusingTrack,
        .focusingPage = focusingPage,
        .selectingTrack = selectingTrack,
        .selectingPage = selectingPage,
        .previewPageAddSlot = previewPageAddSlot,
        .previewTrackAddSlot = previewTrackAddSlot,
        .trackSelectedMask = static_cast<uint16_t>(
            selectingTrack ? sequencer.structureUi.selection.selectedMask.get() : 0U
        ),
        .pageSelectedMask = static_cast<uint16_t>(
            selectingPage ? selectedMask : 0U
        ),
        .trackActivity = trackActivity,
        .leftText = leftText,
        .badgeText = badgeText,
        .dimmed = false,
    };
}

TrackNavigationStripProps buildTrackNavigationStripProps(const SequencerViewModelSource& source) {
    TrackNavigationStripProps props;
    const auto& sequencer = source.sequencer;
    const bool selectingTrack =
        sequencer.structureUi.selection.active.get() &&
        sequencer.structureUi.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool previewAddSlot =
        !sequencer.structureUi.selection.active.get() && sequencer.structureUi.previewAddSlot.get();
    const uint8_t addTrackIndex =
        (previewAddSlot &&
         source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK)
            ? core::state::sequencer::SequencerTrackBankState::clampTrackIndex(
                  sequencer.structureUi.previewTrackIndex.get()
              )
            : TrackNavigationStripProps::TRACK_COUNT;

    props.activeTrack = source.sharedTrackActive.get();
    props.previewTrack =
        selectingTrack
            ? sequencer.structureUi.selection.cursorIndex.get()
            : ((previewAddSlot && addTrackIndex < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT)
                   ? addTrackIndex
                   : source.sharedTrackActive.get());
    props.addTrackIndex = addTrackIndex;
    props.enabledMask = source.sharedTrackEnabledMask.get();
    props.selectedMask = selectingTrack ? sequencer.structureUi.selection.selectedMask.get() : 0;
    props.focusingTrack =
        !sequencer.structureUi.selection.active.get() &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    props.selectingTrack = selectingTrack;
    for (uint8_t i = 0; i < TrackNavigationStripProps::TRACK_COUNT; ++i) {
        props.activity[i] = source.statusBar.trackNoteActivity[i].get();
    }
    return props;
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
        source.sequencer.structureUi.selection.active.get() &&
        source.sequencer.structureUi.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool selectingPattern = source.sequencer.patternQuickControls.selecting.get();
    const bool selectingProperty = source.sequencer.stepPropertyInlineSelector.selecting.get();
    const bool selectingRange = source.sequencer.rangeSelection.active();
    const bool selectingStructure = source.sequencer.structureUi.selection.active.get();
    const char* propertyIcon = visual::propertyIconGlyph(source.sequencer.activeStepProperty.get());

    StripProps props;
    props.visible = true;

    if (selectingStructure) {
        const bool trackScope =
            source.sequencer.structureUi.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
        props.slots[0] = makeIconSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        props.slots[1] = trackScope
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

    if (selectingTrack) {
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

    if (selectingRange) {
        props.slots[0] = makeIconSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        props.slots[1] = makeIconSlot(
            standalone::icons::MIDI_CHANNEL,
            Visual::DIM
        );
        props.slots[2] = makeIconSlot(propertyIcon, Visual::DIM);
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
    const auto& range = source.sequencer.rangeSelection;
    StripProps props;
    props.visible = true;
    const bool trackFocus =
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const uint8_t selectionCount = countSelectedItems(
        source.sequencer.structureUi.selection.selectedMask.get()
    );

    if (source.sequencer.structureUi.selection.active.get()) {
        props.slots[0] = makeIconSlot(
            standalone::icons::ACTION_CLEAR,
            Visual::ACTIVE,
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
            .label = selectionCount > 0 ? "SEL" : "",
        };
        props.slots[2] = makeIconSlot(
            standalone::icons::ACTION_COPY,
            Visual::ACTIVE,
            Tone::CONSTRUCTIVE
        );
        props.slots[2].showLabel = true;
        props.slots[2].label = "DUP";
        return props;
    }

    if (range.active()) {
        using Kind = core::state::sequencer::RangeSelectionKind;
        using Phase = core::state::sequencer::RangeSelectionPhase;

        const auto kind = range.kind.get();
        const auto phase = range.phase.get();

        if (kind == Kind::CLEAR) {
            props.slots[0] = makeIconSlot(
                standalone::icons::ACTION_CLEAR,
                Visual::ACTIVE,
                Tone::DESTRUCTIVE
            );
            if (phase == Phase::SELECT_RANGE) {
                props.slots[1] = makeIconSlot(
                    standalone::icons::ACTION_PLACE_TARGET,
                    Visual::ACTIVE
                );
            } else {
                props.slots[1].visualState = Visual::HIDDEN;
            }
            props.slots[2].visualState = Visual::HIDDEN;
            return props;
        }

        if (kind == Kind::COPY) {
            props.slots[0].visualState = Visual::HIDDEN;
            props.slots[1] = makeIconSlot(
                standalone::icons::ACTION_PLACE_TARGET,
                phase == Phase::SELECT_RANGE || phase == Phase::PASTE_TARGET
                    ? Visual::ACTIVE
                    : Visual::DIM
            );
            props.slots[2] = makeIconSlot(
                phase == Phase::PASTE_TARGET ? standalone::icons::ACTION_PASTE
                                             : standalone::icons::ACTION_COPY,
                phase == Phase::PASTE_TARGET ? Visual::ARMED : Visual::ACTIVE,
                Tone::CONSTRUCTIVE
            );
            return props;
        }
    }

    const bool canPaste = trackFocus
        ? source.structureClipboard.hasSequencerTrack()
        : source.structureClipboard.hasSequencerPage();
    const bool canRemove = trackFocus
        ? source.sequencer.structureUi.previewAddSlot.get() == false &&
              (source.sharedTrackEnabledMask.get() &
               (source.sharedTrackEnabledMask.get() - 1U)) != 0
        : source.sequencer.structureUi.previewAddSlot.get() == false &&
              source.sequencer.activePageCount() > 1U;
    const auto holdAction = source.sequencer.structureUi.hold.action.get();
    const bool removeHoldActive = holdAction == core::state::StructureHoldAction::REMOVE;
    const bool pasteHoldActive = holdAction == core::state::StructureHoldAction::PASTE;

    props.slots[0] = makeIconSlot(
        standalone::icons::ACTION_CLEAR,
        removeHoldActive ? Visual::ARMED : Visual::ACTIVE,
        Tone::DESTRUCTIVE
    );
    props.slots[0].showLabel = canRemove;
    props.slots[0].label = canRemove ? "DEL" : nullptr;
    props.slots[0].holdActive = removeHoldActive;
    props.slots[0].holdStartedAtMs = source.sequencer.structureUi.hold.startedAtMs.get();
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
        pasteHoldActive ? Visual::ARMED : Visual::ACTIVE,
        canPaste ? Tone::CONSTRUCTIVE : Tone::NEUTRAL
    );
    props.slots[2].showLabel = canPaste;
    props.slots[2].label = canPaste ? "PST" : nullptr;
    props.slots[2].holdActive = pasteHoldActive;
    props.slots[2].holdStartedAtMs = source.sequencer.structureUi.hold.startedAtMs.get();
    props.slots[2].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    return props;
}

grid::StepGridFrameState buildStepGridProps(const SequencerViewModelSource& source) {
    return grid::buildStepGridFrameState(source.sequencer);
}

}  // namespace core::ui::sequencer

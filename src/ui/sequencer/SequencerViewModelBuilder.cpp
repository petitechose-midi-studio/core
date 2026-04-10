#include "ui/sequencer/SequencerViewModelBuilder.hpp"

#include <oc/type/TextFormat.hpp>

#include "config/Timing.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/StepGridFrameLogic.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

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

uint8_t nextAddIndexAfterHighest(uint16_t enabledMask, uint8_t count) {
    for (int index = static_cast<int>(count) - 1; index >= 0; --index) {
        if ((enabledMask & static_cast<uint16_t>(1U << static_cast<uint8_t>(index))) == 0) {
            continue;
        }
        const int next = index + 1;
        return (next < count) ? static_cast<uint8_t>(next) : count;
    }
    return 0;
}

}  // namespace

SequencerHeaderBarProps buildHeaderBarProps(const SequencerViewModelSource& source) {
    const auto& sequencer = source.sequencer;
    const auto& tracks = source.tracks;
    const auto& status = source.statusBar;
    const uint8_t activeTrack = tracks.activeTrack.get();
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
    const bool previewAddSlot =
        !sequencer.structureUi.selection.active.get() && sequencer.structureUi.previewAddSlot.get();
    const uint8_t addTrackIndex = nextAddIndexAfterHighest(
        tracks.enabledMask.get(),
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    );
    const uint8_t previewTrack =
        selectingTrack
            ? sequencer.structureUi.selection.cursorIndex.get()
            : ((previewAddSlot &&
                source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK &&
                addTrackIndex < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT)
                   ? addTrackIndex
                   : activeTrack);
    const uint8_t addPageIndex = sequencer.activePageCount();
    const uint8_t viewedPage =
        selectingPage
            ? sequencer.structureUi.selection.cursorIndex.get()
            : ((previewAddSlot &&
                source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE &&
                addPageIndex < core::state::sequencer::SequencerState::PAGE_COUNT)
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

    std::array<uint8_t, SequencerHeaderBarProps::TRACK_COUNT> trackActivity{};
    for (uint8_t i = 0; i < trackActivity.size(); ++i) {
        trackActivity[i] = status.trackNoteActivity[i].get();
    }

    return {
        .length = sequencer.length.get(),
        .activePage = sequencer.visiblePage(),
        .viewedPage = viewedPage,
        .playheadStep = sequencer.playheadStep.get(),
        .activeTrack = activeTrack,
        .previewTrack = previewTrack,
        .addPageIndex = addPageIndex,
        .addTrackIndex = addTrackIndex,
        .enabledMask = tracks.enabledMask.get(),
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
            selectingPage ? sequencer.structureUi.selection.selectedMask.get() : 0U
        ),
        .trackActivity = trackActivity,
        .leftText = leftText,
        .dimmed = false,
    };
}

TrackNavigationStripProps buildTrackNavigationStripProps(const SequencerViewModelSource& source) {
    TrackNavigationStripProps props;
    const auto& sequencer = source.sequencer;
    const auto& tracks = source.tracks;
    const bool selectingTrack =
        sequencer.structureUi.selection.active.get() &&
        sequencer.structureUi.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool previewAddSlot =
        !sequencer.structureUi.selection.active.get() && sequencer.structureUi.previewAddSlot.get();
    const uint8_t addTrackIndex = nextAddIndexAfterHighest(
        tracks.enabledMask.get(),
        core::state::sequencer::SequencerTrackBankState::TRACK_COUNT
    );

    props.activeTrack = tracks.activeTrack.get();
    props.previewTrack =
        selectingTrack
            ? sequencer.structureUi.selection.cursorIndex.get()
            : ((previewAddSlot &&
                source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK &&
                addTrackIndex < core::state::sequencer::SequencerTrackBankState::TRACK_COUNT)
                   ? addTrackIndex
                   : tracks.activeTrack.get());
    props.addTrackIndex = addTrackIndex;
    props.enabledMask = tracks.enabledMask.get();
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

    if (source.sequencer.structureUi.selection.active.get()) {
        props.slots[0] = makeIconSlot(
            standalone::icons::ACTION_CLEAR,
            Visual::ACTIVE,
            Tone::DESTRUCTIVE
        );
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2] = makeIconSlot(
            standalone::icons::ACTION_COPY,
            Visual::ACTIVE,
            Tone::CONSTRUCTIVE
        );
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

    const bool trackFocus =
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool canPaste = trackFocus
        ? source.structureClipboard.hasSequencerTrack()
        : source.structureClipboard.hasSequencerPage();
    const auto holdAction = source.sequencer.structureUi.hold.action.get();
    const bool removeHoldActive = holdAction == core::state::StructureHoldAction::REMOVE;
    const bool pasteHoldActive = holdAction == core::state::StructureHoldAction::PASTE;

    props.slots[0] = makeIconSlot(
        removeHoldActive ? standalone::icons::ACTION_CANCEL : standalone::icons::ACTION_CLEAR,
        removeHoldActive ? Visual::ARMED : Visual::ACTIVE,
        Tone::DESTRUCTIVE
    );
    props.slots[0].holdActive = removeHoldActive;
    props.slots[0].holdStartedAtMs = source.sequencer.structureUi.hold.startedAtMs.get();
    props.slots[0].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    props.slots[1].visualState = Visual::HIDDEN;
    props.slots[2] = makeIconSlot(
        canPaste ? standalone::icons::ACTION_PASTE : standalone::icons::ACTION_COPY,
        pasteHoldActive ? Visual::ARMED : (canPaste ? Visual::ARMED : Visual::ACTIVE),
        canPaste ? Tone::CONSTRUCTIVE : Tone::NEUTRAL
    );
    props.slots[2].holdActive = pasteHoldActive;
    props.slots[2].holdStartedAtMs = source.sequencer.structureUi.hold.startedAtMs.get();
    props.slots[2].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    return props;
}

grid::StepGridFrameState buildStepGridProps(const SequencerViewModelSource& source) {
    return grid::buildStepGridFrameState(source.sequencer);
}

}  // namespace core::ui::sequencer

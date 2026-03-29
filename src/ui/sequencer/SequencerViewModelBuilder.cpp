#include "ui/sequencer/SequencerViewModelBuilder.hpp"

#include <oc/type/TextFormat.hpp>

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

}  // namespace

SequencerHeaderBarProps buildHeaderBarProps(const core::state::CoreState& coreState) {
    const auto& sequencer = coreState.sequencer;
    const auto& tracks = coreState.sequencerTracks;
    const auto& status = coreState.statusBar;
    const uint8_t activeTrack = tracks.activeTrack.get();
    const uint8_t previewTrack =
        tracks.selector.selecting.get() ? tracks.selector.selectedTrack.get() : activeTrack;

    static char leftText[24];
    size_t pos = oc::type::text::appendString(leftText, sizeof(leftText), 0, "Track ");
    pos = oc::type::text::appendUnsigned(
        leftText,
        sizeof(leftText),
        pos,
        static_cast<unsigned>(previewTrack + 1U)
    );
    oc::type::text::terminate(leftText, sizeof(leftText), pos);

    return {
        .length = sequencer.length.get(),
        .viewedPage = sequencer.visiblePage(),
        .playheadStep = sequencer.playheadStep.get(),
        .activeTrack = activeTrack,
        .previewTrack = previewTrack,
        .enabledMask = tracks.enabledMask.get(),
        .selectingTrack = tracks.selector.selecting.get(),
        .trackActivity =
            {status.trackNoteActivity[0].get(),
             status.trackNoteActivity[1].get(),
             status.trackNoteActivity[2].get(),
             status.trackNoteActivity[3].get(),
             status.trackNoteActivity[4].get(),
             status.trackNoteActivity[5].get(),
             status.trackNoteActivity[6].get(),
             status.trackNoteActivity[7].get()},
        .leftText = leftText,
        .centerText = "",
        .rightText = "",
        .dimmed = false,
    };
}

SequencerBottomControlsProps buildBottomControlsProps(const core::state::CoreState& coreState) {
    const auto& sequencer = coreState.sequencer;

    return {
        .selectingQuickControls = sequencer.patternQuickControls.selecting.get(),
        .focusedQuickControl = sequencer.patternQuickControls.focusedItem.get(),
        .offsetSteps = sequencer.patternQuickControls.offsetSteps.get(),
        .stepsPerBeat = sequencer.stepsPerBeat.get(),
        .length = sequencer.length.get(),
    };
}

StepPropertyStripProps buildStepPropertyStripProps(const core::state::CoreState& coreState) {
    const auto& sequencer = coreState.sequencer;

    return {
        .activeProperty = sequencer.activeStepProperty.get(),
        .selecting = sequencer.stepPropertyInlineSelector.selecting.get(),
        .selectedIndex = sequencer.stepPropertyInlineSelector.selectedIndex.get(),
    };
}

ContextActionStripProps buildLeftActionStripProps(const core::state::CoreState& coreState) {
    const bool selectingTrack = coreState.sequencerTracks.selector.selecting.get();
    const bool selectingPattern = coreState.sequencer.patternQuickControls.selecting.get();
    const bool selectingProperty = coreState.sequencer.stepPropertyInlineSelector.selecting.get();
    const bool selectingRange = coreState.sequencer.rangeSelection.active();
    const char* propertyIcon = visual::propertyIconGlyph(coreState.sequencer.activeStepProperty.get());

    StripProps props;
    props.visible = true;

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

ContextActionStripProps buildBottomActionStripProps(const core::state::CoreState& coreState) {
    const auto& range = coreState.sequencer.rangeSelection;
    StripProps props;
    props.visible = true;

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

    props.slots[0] = makeIconSlot(
        standalone::icons::ACTION_CLEAR,
        Visual::DIM,
        Tone::DESTRUCTIVE
    );
    props.slots[1].visualState = Visual::HIDDEN;
    props.slots[2] = makeIconSlot(
        standalone::icons::ACTION_COPY,
        Visual::DIM,
        Tone::CONSTRUCTIVE
    );
    return props;
}

grid::StepGridFrameState buildStepGridProps(const core::state::CoreState& coreState) {
    return grid::buildStepGridFrameState(coreState);
}

}  // namespace core::ui::sequencer

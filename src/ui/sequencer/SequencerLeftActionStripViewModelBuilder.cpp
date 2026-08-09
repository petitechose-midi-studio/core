#include "ui/sequencer/SequencerLeftActionStripViewModelBuilder.hpp"

#include <config/PlatformCompat.hpp>

#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerInteractionContextOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/SequencerQuickControlVisuals.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui::sequencer {

namespace {

using StripProps = core::ui::ContextActionStripProps;
using SlotProps = core::ui::ContextActionStripSlotProps;
using Visual = core::ui::ContextActionStripVisualState;
using InteractionAction = core::state::sequencer::SequencerInteractionAction;
using InteractionVisibility = core::state::sequencer::SequencerInteractionVisibility;

#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
FLASHMEM const char* drumPropertyIcon(
    core::state::sequencer::DrumTrackUxPrototypeProperty property
) {
    using Property = core::state::sequencer::DrumTrackUxPrototypeProperty;
    using StepProperty = core::state::sequencer::StepProperty;
    switch (property) {
        case Property::STATE: return standalone::icons::ACTION_VALIDATE;
        case Property::PROBABILITY:
            return visual::propertyIconGlyph(StepProperty::PROBABILITY);
        case Property::GATE:
            return visual::propertyIconGlyph(StepProperty::GATE);
        case Property::NUDGE:
            return visual::propertyIconGlyph(StepProperty::NUDGE);
        case Property::VELOCITY:
        case Property::COUNT:
        default:
            return visual::propertyIconGlyph(StepProperty::VELOCITY);
    }
}

FLASHMEM const char* drumDimensionIcon(
    core::state::sequencer::DrumTrackUxPrototypeDimension dimension
) {
    using Dimension = core::state::sequencer::DrumTrackUxPrototypeDimension;
    switch (dimension) {
        case Dimension::MODE: return standalone::icons::ROUTE_PIN;
        case Dimension::DIVISION: return standalone::icons::DIVISION;
        case Dimension::LENGTH:
        case Dimension::COUNT:
        default: return standalone::icons::LENGTH;
    }
}
#endif

Visual visualForInteractionVisibility(InteractionVisibility visibility) {
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

void setStripIconFromVisibility(
    SlotProps& slot,
    const char* icon,
    InteractionVisibility visibility
) {
    const auto visualState = visualForInteractionVisibility(visibility);
    if (visualState == Visual::HIDDEN) {
        slot.visualState = Visual::HIDDEN;
        return;
    }
    slot = core::ui::makeStandaloneIconStripSlot(icon, visualState);
}

const char* iconForLeftAction(
    InteractionAction action,
    const char* patternIcon,
    const char* propertyIcon
) {
    switch (action) {
        case InteractionAction::OPEN_PATTERN_DIMENSION_SELECTOR:
        case InteractionAction::APPLY_PATTERN_DIMENSION_SELECTOR:
            return patternIcon;
        case InteractionAction::OPEN_MUSICAL_PROPERTY_SELECTOR:
        case InteractionAction::APPLY_MUSICAL_PROPERTY_SELECTOR:
            return propertyIcon;
        case InteractionAction::EDIT_STEP_LOCAL_RANDOM:
            return standalone::icons::NOTE_PROP_RANDOM;
        case InteractionAction::OPEN_STEP_CONTENT_SELECTOR:
        case InteractionAction::APPLY_STEP_CONTENT_SELECTOR:
            return standalone::icons::NOTE_PROP_RANDOM;
        case InteractionAction::RETARGET_STEP_EDITOR:
            return standalone::icons::ACTION_PLACE_TARGET;
        case InteractionAction::ENTER_SELECTION:
            return standalone::icons::ACTION_PLACE_TARGET;
        default:
            return nullptr;
    }
}

FLASHMEM void setStripIconFromAction(
    SlotProps& slot,
    InteractionAction action,
    InteractionVisibility visibility,
    const char* patternIcon,
    const char* propertyIcon
) {
    const char* icon = iconForLeftAction(action, patternIcon, propertyIcon);
    if (icon == nullptr) {
        slot.visualState = Visual::HIDDEN;
        return;
    }
    setStripIconFromVisibility(slot, icon, visibility);
}

}  // namespace

FLASHMEM ContextActionStripProps buildSequencerLeftActionStripProps(
    const SequencerViewModelSource& source
) {
#if defined(MS_DRUM_TRACK_UX_PROTOTYPE)
    if (source.sequencer.drumTrackUxPrototype.active()) {
        const auto& prototype = source.sequencer.drumTrackUxPrototype;
        StripProps props;
        props.visible = true;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        if (!prototype.gridVisible()) return props;

        const bool stepFocus = source.navigationFocus.get() ==
            core::state::StructureNavigationFocus::STEP;
        if (prototype.selector == core::state::sequencer::
                DrumTrackUxPrototypeSelector::DIMENSION) {
            props.slots[1] = core::ui::makeStandaloneIconStripSlot(
                drumDimensionIcon(prototype.dimension),
                Visual::ACTIVE
            );
            return props;
        }
        if (prototype.selector == core::state::sequencer::
                DrumTrackUxPrototypeSelector::PROPERTY) {
            props.slots[stepFocus ? 1U : 2U] =
                core::ui::makeStandaloneIconStripSlot(
                    drumPropertyIcon(prototype.property),
                    Visual::ACTIVE
                );
            return props;
        }

        if (stepFocus) {
            props.slots[1] = core::ui::makeStandaloneIconStripSlot(
                drumPropertyIcon(prototype.property),
                Visual::ACTIVE
            );
        } else {
            props.slots[1] = core::ui::makeStandaloneIconStripSlot(
                drumDimensionIcon(prototype.dimension),
                Visual::ACTIVE
            );
            props.slots[2] = core::ui::makeStandaloneIconStripSlot(
                drumPropertyIcon(prototype.property),
                Visual::ACTIVE
            );
        }
        return props;
    }
#endif

    const bool selectingPattern = source.sequencer.patternQuickControls.selecting.get();
    const bool selectingProperty = source.sequencer.stepPropertyInlineSelector.selecting.get();
    const bool selectingStepContent = source.sequencer.stepContentSelector.selecting.get();
    const bool selectingTrack = source.trackNavigation.selection.active.get();
    const bool selectingPage =
        source.sequencer.structureUi.pageSelection.active.get();
    const bool selectingStep = source.sequencer.structureUi.stepSelection.active.get();
    const bool selectingStructure =
        selectingTrack || selectingPage || selectingStep;
    const auto interaction = core::state::sequencer::buildSequencerInteractionPolicy(
        core::state::sequencer::makeSequencerInteractionContext(
            source.sequencer,
            source.trackNavigation,
            source.navigationFocus.get()
        )
    );
    const char* propertyIcon = source.sequencer.stepStatePropertyActive.get()
        ? standalone::icons::ACTION_VALIDATE
        : visual::propertyIconGlyph(source.sequencer.activeStepProperty.get());
    const char* patternIcon = visual::quickControlIconGlyph(
        source.sequencer.patternQuickControls.focusedItem.get()
    );

    StripProps props;
    props.visible = true;

    if (source.sequencer.ccLaneUi.mode ==
        core::state::sequencer::SequencerCcLaneUiMode::LANE_GRID) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::MIDI_CC,
            Visual::ACTIVE
        );
        return props;
    }

    if (selectingStructure) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    if (selectingPattern) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        setStripIconFromAction(
            props.slots[1],
            interaction.leftCenterPress,
            interaction.leftCenterVisibility,
            patternIcon,
            propertyIcon
        );
        setStripIconFromAction(
            props.slots[2],
            interaction.leftBottomPress,
            interaction.leftBottomVisibility,
            patternIcon,
            propertyIcon
        );
        return props;
    }

    if (selectingProperty) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        setStripIconFromAction(
            props.slots[1],
            interaction.leftCenterPress,
            interaction.leftCenterVisibility,
            patternIcon,
            propertyIcon
        );
        setStripIconFromAction(
            props.slots[2],
            interaction.leftBottomPress,
            interaction.leftBottomVisibility,
            patternIcon,
            propertyIcon
        );
        return props;
    }

    if (selectingStepContent) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        props.slots[1].visualState = Visual::HIDDEN;
        setStripIconFromAction(
            props.slots[2],
            interaction.leftBottomPress,
            interaction.leftBottomVisibility,
            patternIcon,
            propertyIcon
        );
        return props;
    }

    if (core::state::sequencer::isChildContentView(source.sequencer)) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_BACKWARD,
            Visual::ACTIVE
        );
    } else {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_PLACE_TARGET,
            Visual::ACTIVE
        );
    }
    setStripIconFromAction(
        props.slots[1],
        interaction.leftCenterPress,
        interaction.leftCenterVisibility,
        patternIcon,
        propertyIcon
    );
    setStripIconFromAction(
        props.slots[2],
        interaction.leftBottomPress,
        interaction.leftBottomVisibility,
        patternIcon,
        propertyIcon
    );
    return props;
}

}  // namespace core::ui::sequencer

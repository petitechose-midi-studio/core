#include "ui/sequencer/SequencerViewModelBuilder.hpp"

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerInteractionContextOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerStepPastePlan.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/SequencerBottomActionStripViewModelBuilder.hpp"
#include "ui/sequencer/SequencerHeaderViewModelBuilder.hpp"
#include "ui/sequencer/SequencerPropertyOverlayViewModelBuilder.hpp"
#include "ui/sequencer/SequencerQuickControlVisuals.hpp"
#include "ui/sequencer/StepGridFrameLogic.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"

namespace core::ui::sequencer {

namespace {

using StripProps = core::ui::ContextActionStripProps;
using SlotProps = core::ui::ContextActionStripSlotProps;
using Visual = core::ui::ContextActionStripVisualState;
using InteractionAction = core::state::sequencer::SequencerInteractionAction;
using InteractionVisibility = core::state::sequencer::SequencerInteractionVisibility;

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

const char* iconForLeftAction(InteractionAction action,
                              const char* patternIcon,
                              const char* propertyIcon) {
    switch (action) {
        case InteractionAction::OPEN_PATTERN_DIMENSION_SELECTOR:
        case InteractionAction::APPLY_PATTERN_DIMENSION_SELECTOR:
            return patternIcon;
        case InteractionAction::OPEN_MUSICAL_PROPERTY_SELECTOR:
        case InteractionAction::APPLY_MUSICAL_PROPERTY_SELECTOR:
            return propertyIcon;
        case InteractionAction::EDIT_STEP_LOCAL_RANDOM:
            return standalone::icons::NOTE_PROP_RANDOM;
        default:
            return nullptr;
    }
}

void setStripIconFromAction(SlotProps& slot,
                            InteractionAction action,
                            InteractionVisibility visibility,
                            const char* patternIcon,
                            const char* propertyIcon) {
    const char* icon = iconForLeftAction(action, patternIcon, propertyIcon);
    if (icon == nullptr) {
        slot.visualState = Visual::HIDDEN;
        return;
    }
    setStripIconFromVisibility(slot, icon, visibility);
}

void applyStepPasteFootprint(
    grid::StepGridFrameState& frame,
    const SequencerViewModelSource& source
) {
    const auto& selection = source.sequencer.structureUi.stepSelection;
    if (!selection.active.get() || !source.structureClipboard.hasSequencerSteps()) return;

    const auto mode = core::state::project::sanitizeProjectStepPasteMode(
        source.projectNavigation.stepPasteMode
    );
    const uint8_t activeLength = core::state::sequencer::activeContentLength(source.sequencer);
    const uint8_t maxStep = core::state::sequencer::maxStepCursorForPaste(source.sequencer);
    const auto plan = core::state::sequencer::buildStepPastePreviewPlan(
        source.structureClipboard.sequencerSteps,
        core::state::sequencer::isRootContentView(source.sequencer),
        selection.cursorStep.get(),
        activeLength,
        maxStep,
        mode
    );

    for (uint8_t i = 0; i < plan.count; ++i) {
        const auto& entry = plan.entries[i];
        if (!entry.valid) continue;
        for (auto& tile : frame.tiles) {
            if (tile.absoluteStep != entry.targetStep) continue;
            tile.stepPastePreviewActive = true;
            tile.stepPastePreview = entry.preview;
            break;
        }
    }

    if (!plan.blocked) return;
    for (auto& tile : frame.tiles) {
        if (!tile.stepSelectionCursor) continue;
        tile.stepPastePreviewActive = true;
        tile.stepPastePreview = core::state::sequencer::SequencerStepPastePreview::BLOCKED;
        return;
    }
}

}  // namespace

FLASHMEM SequencerHeaderBarProps buildHeaderBarProps(const SequencerViewModelSource& source) {
    return buildSequencerHeaderBarProps(source);
}

FLASHMEM StepPropertySelectionOverlayProps buildPropertySelectionOverlayProps(
    const SequencerViewModelSource& source
) {
    return buildSequencerPropertySelectionOverlayProps(source);
}

FLASHMEM ContextActionStripProps buildLeftActionStripProps(const SequencerViewModelSource& source) {
    const bool selectingTrack =
        source.trackNavigation.selection.active.get() &&
        source.trackNavigation.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool physicalQuickControlHold =
        source.sequencer.patternQuickControls.physicalHoldActive.get();
    const bool selectingPattern = source.sequencer.patternQuickControls.selecting.get();
    const bool selectingProperty = source.sequencer.stepPropertyInlineSelector.selecting.get();
    const bool selectingPage = source.sequencer.structureUi.pageSelection.active.get();
    const bool selectingStep = source.sequencer.structureUi.stepSelection.active.get();
    const bool selectingStructure = selectingTrack || selectingPage || selectingStep;
    const auto interaction = core::state::sequencer::buildSequencerInteractionPolicy(
        core::state::sequencer::makeSequencerInteractionContext(
            source.sequencer,
            source.trackNavigation,
            source.navigationFocus.get()
        )
    );
    const char* propertyIcon = visual::propertyIconGlyph(source.sequencer.activeStepProperty.get());
    const char* patternIcon =
        visual::quickControlIconGlyph(source.sequencer.patternQuickControls.focusedItem.get());

    StripProps props;
    props.visible = true;

    if (selectingStructure) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_CANCEL,
            Visual::ACTIVE
        );
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    if (physicalQuickControlHold) {
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_UNDO,
            Visual::DIM
        );
        props.slots[1] = core::ui::makeStandaloneIconStripSlot(
            patternIcon,
            Visual::ACTIVE
        );
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::ACTION_REDO,
            Visual::DIM
        );
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

    props.slots[0].visualState = Visual::HIDDEN;
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

FLASHMEM ContextActionStripProps buildBottomActionStripProps(const SequencerViewModelSource& source) {
    return buildSequencerBottomActionStripProps(source);
}

FLASHMEM grid::StepGridFrameState buildStepGridProps(const SequencerViewModelSource& source) {
    auto frame = grid::buildStepGridFrameState(
        source.sequencer,
        source.tracks.projectScaleSettings(),
        source.navigationFocus.get() == core::state::StructureNavigationFocus::STEP
    );
    applyStepPasteFootprint(frame, source);
    return frame;
}

}  // namespace core::ui::sequencer

#include "ui/sequencer/SequencerViewModelBuilder.hpp"

#include <oc/type/TextFormat.hpp>

#include <config/PlatformCompat.hpp>

#include "state/project/ProjectDomainRules.hpp"
#include "state/sequencer/SequencerContentViewOps.hpp"
#include "state/sequencer/SequencerGraphOps.hpp"
#include "state/sequencer/SequencerInteractionContextOps.hpp"
#include "state/sequencer/SequencerInteractionPolicy.hpp"
#include "state/sequencer/SequencerQuickControls.hpp"
#include "state/sequencer/SequencerResolvedDisplayProjectionOps.hpp"
#include "state/sequencer/SequencerScaleState.hpp"
#include "state/sequencer/SequencerStepPastePlan.hpp"
#include "state/sequencer/StepPropertyDisplay.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/sequencer/SequencerBottomActionStripViewModelBuilder.hpp"
#include "ui/sequencer/SequencerHeaderViewModelBuilder.hpp"
#include "ui/sequencer/StepGridFrameLogic.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

#include <cstdio>

namespace core::ui::sequencer {

namespace theme = standalone::theme;

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

using QuickItem = core::state::sequencer::PatternQuickControlItem;

const char* quickControlIcon(QuickItem item) {
    switch (item) {
        case QuickItem::LENGTH:
            return standalone::icons::LENGTH;
        case QuickItem::DIVISION:
            return standalone::icons::DIVISION;
        case QuickItem::SWING:
            return standalone::icons::SWING;
        case QuickItem::NUDGE:
            return standalone::icons::NOTE_PROP_NUDGE;
        case QuickItem::OFFSET:
        default:
            return standalone::icons::OFFSET;
    }
}

uint32_t quickControlColor(QuickItem item) {
    switch (item) {
        case QuickItem::LENGTH:
            return theme::color::STEP_LENGTH;
        case QuickItem::DIVISION:
            return theme::color::STEP_DIVISION;
        case QuickItem::SWING:
            return theme::color::STEP_SWING;
        case QuickItem::NUDGE:
            return theme::color::STEP_PATTERN_NUDGE;
        case QuickItem::OFFSET:
        default:
            return theme::color::STEP_OFFSET;
    }
}

void formatQuickControlValue(
    char* buffer,
    size_t size,
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::project::ProjectNavigationState& projectNavigation,
    QuickItem item
) {
    if (!buffer || size == 0) return;
    if (core::state::sequencer::isChildContentView(sequencer) &&
        item == QuickItem::DIVISION) {
        buffer[0] = '\0';
        return;
    }

    switch (item) {
        case QuickItem::OFFSET:
            oc::type::text::formatSigned(
                buffer,
                size,
                sequencer.patternQuickControls.offsetSteps.get(),
                true
            );
            return;
        case QuickItem::DIVISION:
            oc::type::text::formatFraction(
                buffer,
                size,
                1U,
                static_cast<unsigned>(
                    4U * static_cast<uint16_t>(sequencer.pattern.stepsPerBeat.get())
                )
            );
            return;
        case QuickItem::SWING:
            std::snprintf(
                buffer,
                size,
                "%u%%",
                static_cast<unsigned>(
                    sequencer.pattern.effectiveSwingPercent(
                        projectNavigation.transportSwingPercent
                    )
                )
            );
            return;
        case QuickItem::NUDGE:
            std::snprintf(
                buffer,
                size,
                "%+d%%",
                static_cast<int>(sequencer.pattern.patternNudgePercent.get())
            );
            return;
        case QuickItem::LENGTH:
        default:
            oc::type::text::formatUnsigned(
                buffer,
                size,
                static_cast<unsigned>(
                    core::state::sequencer::activeContentLength(sequencer)
                )
            );
            return;
    }
}

uint8_t localVariationRangeForStep(
    const core::state::sequencer::SequencerState& sequencer,
    core::state::sequencer::StepProperty property
) {
    if (property == core::state::sequencer::StepProperty::PROBABILITY) return 0;

    const auto& selector = sequencer.stepPropertyInlineSelector;
    if (selector.localVariationStepIndex >=
        core::state::sequencer::activeContentLength(sequencer)) {
        return 0;
    }

    const auto* graph = core::state::sequencer::graphView(sequencer.pattern);
    if (graph == nullptr) return 0;

    const auto nodeId = core::state::sequencer::activeContentStepNodeId(
        sequencer,
        selector.localVariationStepIndex
    );
    const auto* node = graph->stepNode(nodeId);
    return node ? core::state::sequencer::nodeLocalVariationRange(*node, property) : 0;
}

void formatLocalVariationRangeText(
    char* buffer,
    size_t size,
    core::state::sequencer::StepProperty property,
    uint8_t range,
    bool pitchUsesScaleDegrees
) {
    if (!buffer || size == 0) return;
    if (!core::state::sequencer::stepPropertySupportsLocalVariation(property)) {
        std::snprintf(buffer, size, "--");
        return;
    }

    const char* unit = "";
    if (property == core::state::sequencer::StepProperty::GATE) {
        unit = "%";
    } else if (property == core::state::sequencer::StepProperty::NOTE &&
               pitchUsesScaleDegrees) {
        unit = "d";
    } else if (property == core::state::sequencer::StepProperty::NOTE) {
        unit = "st";
    }

    std::snprintf(buffer, size, "±%u%s", static_cast<unsigned>(range), unit);
}

void formatLocalVariationOverlayValue(
    char* buffer,
    size_t size,
    const core::state::sequencer::SequencerState& sequencer,
    const core::state::sequencer::SequencerTrackBankState& tracks,
    core::state::sequencer::StepProperty property,
    uint8_t step,
    uint8_t range
) {
    if (!buffer || size == 0) return;

    const auto displayContext =
        core::state::sequencer::makeSequencerResolvedDisplayProjectionContext(
            sequencer,
            tracks.projectScaleSettings(),
            property
        );
    char rangeText[8] = {};
    formatLocalVariationRangeText(
        rangeText,
        sizeof(rangeText),
        property,
        range,
        displayContext.scaleSettings.isConstrained()
    );
    if (!core::state::sequencer::stepPropertySupportsLocalVariation(property)) {
        std::snprintf(buffer, size, "%s", rangeText);
        return;
    }

    const auto touchedMask = sequencer.stepInlineFeedback.touchedMask.get();
    const bool stepInlineEditActive =
        sequencer.stepInlineFeedback.visible.get() && touchedMask.test(step);
    const auto resolved =
        core::state::sequencer::buildSequencerResolvedStepDisplayState(
            displayContext,
            step,
            stepInlineEditActive
        );
    if (!resolved.valid) {
        std::snprintf(buffer, size, "%s", rangeText);
        return;
    }

    const auto values =
        core::state::sequencer::sequencerResolvedStepDisplayValues(resolved);
    char valueText[8] = {};
    core::state::sequencer::formatStepPropertyValue(
        valueText,
        sizeof(valueText),
        property,
        values.note,
        values.velocity,
        values.gate,
        values.nudge,
        resolved.probability
    );
    std::snprintf(buffer, size, "%s %s", valueText, rangeText);
}

}  // namespace

FLASHMEM SequencerHeaderBarProps buildHeaderBarProps(const SequencerViewModelSource& source) {
    return buildSequencerHeaderBarProps(source);
}

FLASHMEM StepPropertySelectionOverlayProps buildPropertySelectionOverlayProps(
    const SequencerViewModelSource& source
) {
    const auto& sequencer = source.sequencer;

    if (sequencer.stepPropertyInlineSelector.selecting.get()) {
        if (sequencer.stepPropertyInlineSelector.macroLocalVariationEditActive.get()) {
            const auto property = sequencer.activeStepProperty.get();
            StepPropertySelectionOverlayProps props{
                .visible = true,
                .property = property,
                .customContent = true,
                .icon = visual::propertyIconGlyph(property),
                .label = semantic::labelForProperty(property),
                .useValueText = true,
                .color = semantic::colorForProperty(property),
            };
            formatLocalVariationOverlayValue(
                props.valueText.data(),
                props.valueText.size(),
                sequencer,
                source.tracks,
                property,
                sequencer.stepPropertyInlineSelector.localVariationStepIndex,
                localVariationRangeForStep(sequencer, property)
            );
            return props;
        }

        return {
            .visible = true,
            .property = sequencer.activeStepProperty.get(),
        };
    }

    if (sequencer.patternQuickControls.selecting.get() ||
        sequencer.patternQuickControls.feedbackVisible.get()) {
        const auto item = sequencer.patternQuickControls.focusedItem.get();
        StepPropertySelectionOverlayProps props{
            .visible = true,
            .customContent = true,
            .icon = quickControlIcon(item),
            .label = core::state::sequencer::quickControlLabel(item),
            .useValueText = true,
            .color = quickControlColor(item),
        };
        formatQuickControlValue(
            props.valueText.data(),
            props.valueText.size(),
            sequencer,
            source.projectNavigation,
            item
        );
        return props;
    }

    return {.visible = false};
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
        quickControlIcon(source.sequencer.patternQuickControls.focusedItem.get());

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

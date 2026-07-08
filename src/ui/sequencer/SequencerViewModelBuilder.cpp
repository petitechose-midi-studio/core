#include "ui/sequencer/SequencerViewModelBuilder.hpp"

#include <oc/type/TextFormat.hpp>

#include <config/PlatformCompat.hpp>

#include "config/Timing.hpp"
#include "state/StructureClipboardPastePlan.hpp"
#include "state/project/ProjectDomainRules.hpp"
#include "state/shared/StructureSlotOps.hpp"
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
#include "ui/sequencer/SequencerActionStripVisuals.hpp"
#include "ui/sequencer/StepGridFrameLogic.hpp"
#include "ui/sequencer/StepPropertyVisuals.hpp"
#include "ui/sequencer/StepSemanticVisuals.hpp"
#include "ui/theme/StandaloneTheme.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace core::ui::sequencer {

namespace theme = standalone::theme;
namespace structure_slots = core::state::shared;

namespace {

using StripProps = core::ui::ContextActionStripProps;
using SlotProps = core::ui::ContextActionStripSlotProps;
using Visual = core::ui::ContextActionStripVisualState;
using Tone = core::ui::ContextActionStripTone;
using InteractionAction = core::state::sequencer::SequencerInteractionAction;
using InteractionVisibility = core::state::sequencer::SequencerInteractionVisibility;

uint8_t countSelectedItems(uint16_t mask) {
    uint8_t count = 0;
    while (mask != 0) {
        count += static_cast<uint8_t>(mask & 1U);
        mask >>= 1U;
    }
    return count;
}

uint8_t countSelectedSteps(oc::note::sequencer::StepBitMask128 mask, uint8_t limit) {
    uint8_t count = 0;
    for (uint8_t i = 0; i < limit; ++i) {
        if (mask.test(i)) ++count;
    }
    return count;
}

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

void formatSelectionLabel(std::array<char, 16>& out, uint8_t count) {
    std::snprintf(
        out.data(),
        out.size(),
        "SEL %u",
        static_cast<unsigned>(count)
    );
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

Tone variationStatusTone(core::state::sequencer::StepProperty property) {
    switch (property) {
        case core::state::sequencer::StepProperty::VELOCITY:
            return Tone::WARNING;
        case core::state::sequencer::StepProperty::GATE:
            return Tone::POSITIVE;
        case core::state::sequencer::StepProperty::NUDGE:
            return Tone::CONSTRUCTIVE;
        case core::state::sequencer::StepProperty::NOTE:
        case core::state::sequencer::StepProperty::PROBABILITY:
        default:
            return Tone::NEUTRAL;
    }
}

bool focusedStepHasChildContent(const SequencerViewModelSource& source) {
    const auto& sequencer = source.sequencer;
    const auto nodeId = core::state::sequencer::activeContentStepNodeId(
        sequencer,
        sequencer.focusedStep.get()
    );
    return core::state::sequencer::stepNodeHasAnyChildContent(sequencer.pattern, nodeId);
}

bool canPasteStepContent(const SequencerViewModelSource& source) {
    return source.structureClipboard.hasSequencerStepContent(
               core::state::SequencerStepContentClipboardKind::ALL
           ) &&
           core::state::sequencer::activeContentStepCanReceiveChildContent(
               source.sequencer,
               source.sequencer.focusedStep.get()
           );
}

core::state::sequencer::SequencerInteractionContext makeBottomInteractionContext(
    const SequencerViewModelSource& source
) {
    auto context = core::state::sequencer::makeSequencerInteractionContext(
        source.sequencer,
        source.trackNavigation,
        source.navigationFocus.get()
    );
    const bool trackFocus =
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;

    if (context.stepSelectionActive) {
        context.selectedItemsAvailable =
            countSelectedSteps(
                source.sequencer.structureUi.stepSelection.selectedMask.get(),
                core::state::sequencer::activeContentLength(source.sequencer)
            ) > 0;
        context.compatibleClipboardAvailable =
            source.structureClipboard.hasSequencerSteps() &&
            source.structureClipboard.sequencerSteps.rootContext ==
                core::state::sequencer::isRootContentView(source.sequencer);
        return context;
    }

    if (context.trackSelectionActive) {
        const uint16_t actionableMask = static_cast<uint16_t>(
            source.trackNavigation.selection.selectedMask.get() &
            source.sharedTrackEnabledMask.get()
        );
        context.selectedItemsAvailable = countSelectedItems(actionableMask) > 0;
        context.compatibleClipboardAvailable =
            source.structureClipboard.hasSequencerTrackSelection();
        return context;
    }

    if (context.pageSelectionActive) {
        const uint16_t actionableMask = static_cast<uint16_t>(
            source.sequencer.structureUi.pageSelection.selectedMask.get() &
            structure_slots::prefixMask(source.sequencer.activePageCount())
        );
        context.selectedItemsAvailable = countSelectedItems(actionableMask) > 0;
        context.compatibleClipboardAvailable =
            source.structureClipboard.hasSequencerPageSelection();
        return context;
    }

    if (source.navigationFocus.get() == core::state::StructureNavigationFocus::STEP) {
        const bool focusedStepValid =
            source.sequencer.focusedStep.get() <
            core::state::sequencer::activeContentLength(source.sequencer);
        context.currentStructureCanClear = focusedStepValid;
        context.currentStructureCanRemove = focusedStepValid;
        context.currentStructureCanCopy = focusedStepValid;
        context.compatibleClipboardAvailable =
            source.structureClipboard.hasSequencerSteps() &&
            source.structureClipboard.sequencerSteps.rootContext ==
                core::state::sequencer::isRootContentView(source.sequencer);
        return context;
    }

    context.currentStructureCanClear = !context.previewingAddSlot;
    context.currentStructureCanCopy = !context.previewingAddSlot;

    if (context.childContentView) {
        context.currentStepHasChildContent = focusedStepHasChildContent(source);
        context.compatibleClipboardAvailable = canPasteStepContent(source);
        return context;
    }
    if (trackFocus) {
        context.currentStructureCanRemove =
            !context.previewingAddSlot &&
            countSelectedItems(source.sharedTrackEnabledMask.get()) > 1;
        context.compatibleClipboardAvailable = source.structureClipboard.hasSequencerTrack();
    } else {
        context.currentStructureCanRemove =
            !context.previewingAddSlot && source.sequencer.activePageCount() > 1;
        context.compatibleClipboardAvailable = source.structureClipboard.hasSequencerPage();
    }
    return context;
}

void formatVariationStatusLabel(std::array<char, 16>& out,
                                core::state::sequencer::StepProperty property,
                                uint8_t range) {
    constexpr const char* plusMinus = "\xC2\xB1";

    if (property == core::state::sequencer::StepProperty::PROBABILITY) {
        std::snprintf(out.data(), out.size(), "--");
        return;
    }

    std::snprintf(
        out.data(),
        out.size(),
        "%s%u",
        plusMinus,
        static_cast<unsigned>(range)
    );
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
        source.trackNavigation.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool selectingPage =
        sequencer.structureUi.pageSelection.active.get() &&
        sequencer.structureUi.pageSelection.scope.get() == core::state::StructureSelectionScope::PAGE;
    const bool selectingStep = sequencer.structureUi.stepSelection.active.get();
    const uint16_t pageSelectionMask = sequencer.structureUi.pageSelection.selectedMask.get();
    const bool previewAddTrackSlot =
        !source.trackNavigation.selection.active.get() && source.trackNavigation.previewAddSlot.get();
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
                  static_cast<uint8_t>(core::state::sequencer::SequencerState::PAGE_COUNT - 1U)
              )
        : selectingPage
            ? sequencer.structureUi.pageSelection.cursorIndex.get()
            : ((previewAddPageSlot && addPageIndex < core::state::sequencer::SequencerState::PAGE_COUNT)
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
        .activePage = core::state::sequencer::activeContentPageForStep(sequencer.focusedStep.get()),
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
    StripProps props;
    props.visible = true;
    const bool trackFocus =
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool selectingTrack = source.trackNavigation.selection.active.get();
    const bool selectingPage = source.sequencer.structureUi.pageSelection.active.get();
    const bool selectingStep = source.sequencer.structureUi.stepSelection.active.get();
    const bool selectingPatternVariation = source.sequencer.stepPropertyInlineSelector.selecting.get();
    const uint16_t selectionMask = selectingTrack
        ? source.trackNavigation.selection.selectedMask.get()
        : source.sequencer.structureUi.pageSelection.selectedMask.get();

    if (selectingPatternVariation) {
        const auto property = source.sequencer.activeStepProperty.get();
        const uint8_t range = source.sequencer.variationRangeForProperty(property);
        const char* propertyIcon = visual::propertyIconGlyph(property);
        const bool canOpenPitchSettings =
            property == core::state::sequencer::StepProperty::NOTE;

        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            standalone::icons::SETTINGS_GEAR,
            canOpenPitchSettings ? Visual::ACTIVE : Visual::HIDDEN
        );
        props.slots[1] = SlotProps{
            .visualState = property == core::state::sequencer::StepProperty::PROBABILITY
                ? Visual::DISABLED
                : Visual::ACTIVE,
            .tone = variationStatusTone(property),
            .showIcon = true,
            .icon = propertyIcon,
            .iconUsesStandaloneFont = true,
            .iconSize = standalone::icons::Size::L,
            .showLabel = true,
            .label = nullptr,
        };
        formatVariationStatusLabel(props.slots[1].labelText, property, range);
        props.slots[2].visualState = Visual::HIDDEN;
        return props;
    }

    const auto bottomContext = makeBottomInteractionContext(source);
    const auto interaction = core::state::sequencer::buildSequencerInteractionPolicy(
        bottomContext
    );

    if (selectingStep) {
        const uint8_t selectedCount =
            countSelectedSteps(
                source.sequencer.structureUi.stepSelection.selectedMask.get(),
                core::state::sequencer::activeContentLength(source.sequencer)
            );
        const bool canClear = selectedCount > 0;
        const bool canPaste =
            source.structureClipboard.hasSequencerSteps() &&
            source.structureClipboard.sequencerSteps.rootContext ==
                core::state::sequencer::isRootContentView(source.sequencer);
        const bool canCopy = selectedCount > 0;
        const bool pastePreviewActive =
            source.sequencer.structureUi.stepSelection.pastePreviewActive.get() && canPaste;
        const auto& holdState = source.sequencer.structureUi.pageHold;
        const auto holdAction = holdState.action.get();
        const bool removeHoldActive =
            holdAction == core::state::StructureHoldAction::REMOVE && canClear;
        const bool pasteHoldActive =
            holdAction == core::state::StructureHoldAction::PASTE && canPaste;
        const auto rightAction = pasteHoldActive || pastePreviewActive || (!canCopy && canPaste)
            ? interaction.bottomRightHold
            : interaction.bottomRightTap;
        const auto leftAction = interaction.bottomLeftHold;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(leftAction),
            removeHoldActive ? Visual::ARMED : (canClear ? Visual::ACTIVE : Visual::DISABLED),
            removeHoldActive ? Tone::DESTRUCTIVE : Tone::WARNING
        );
        props.slots[0].holdActive = removeHoldActive;
        props.slots[0].holdStartedAtMs = holdState.startedAtMs.get();
        props.slots[0].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
        props.slots[1] = SlotProps{
            .visualState = Visual::ACTIVE,
            .tone = Tone::NEUTRAL,
            .showIcon = false,
            .icon = nullptr,
            .showLabel = true,
        };
        formatSelectionLabel(props.slots[1].labelText, selectedCount);
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(rightAction),
            pasteHoldActive
                ? Visual::ARMED
                : ((canCopy || canPaste) ? Visual::ACTIVE : Visual::DISABLED),
            (pasteHoldActive || pastePreviewActive) ? Tone::POSITIVE : Tone::NEUTRAL
        );
        props.slots[2].holdActive = pasteHoldActive;
        props.slots[2].holdStartedAtMs = holdState.startedAtMs.get();
        props.slots[2].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
        return props;
    }

    if (core::state::sequencer::isChildContentView(source.sequencer)) {
        const bool hasChildContent = bottomContext.currentStepHasChildContent;
        const bool canPaste = bottomContext.compatibleClipboardAvailable;
        const auto rightAction = hasChildContent || !canPaste
            ? interaction.bottomRightTap
            : interaction.bottomRightHold;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(interaction.bottomLeftTap),
            hasChildContent ? Visual::ACTIVE : Visual::DISABLED,
            Tone::DESTRUCTIVE
        );
        props.slots[1].visualState = Visual::HIDDEN;
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(rightAction),
            (hasChildContent || canPaste) ? Visual::ACTIVE : Visual::DISABLED,
            (!hasChildContent && canPaste) ? Tone::POSITIVE : Tone::NEUTRAL
        );
        return props;
    }

    if (selectingTrack || selectingPage) {
        bool canDeleteSelection = false;
        bool canCopySelection = false;
        bool canPasteSelection = false;
        if (selectingTrack) {
            const uint16_t enabledMask = source.sharedTrackEnabledMask.get();
            const uint16_t actionableMask = static_cast<uint16_t>(selectionMask & enabledMask);
            const uint8_t actionableCount = countSelectedItems(actionableMask);
            const uint8_t enabledCount = countSelectedItems(enabledMask);
            canDeleteSelection = actionableCount > 0 && actionableCount < enabledCount;
            canCopySelection = actionableCount > 0;
            canPasteSelection = source.structureClipboard.hasSequencerTrackSelection();
        } else {
            const uint8_t activePages = source.sequencer.activePageCount();
            const uint16_t actionableMask = static_cast<uint16_t>(
                selectionMask & structure_slots::prefixMask(activePages)
            );
            const uint8_t actionableCount = countSelectedItems(actionableMask);
            canDeleteSelection = actionableCount > 0 && actionableCount < activePages;
            canCopySelection = actionableCount > 0;
            canPasteSelection = source.structureClipboard.hasSequencerPageSelection();
        }

        const auto& holdState = selectingTrack ? source.trackNavigation.hold
                                               : source.sequencer.structureUi.pageHold;
        const bool deleteHoldActive =
            holdState.action.get() == core::state::StructureHoldAction::REMOVE &&
            canDeleteSelection;
        const bool pasteHoldActive =
            holdState.action.get() == core::state::StructureHoldAction::PASTE &&
            canPasteSelection;
        const auto rightAction = pasteHoldActive || (!canCopySelection && canPasteSelection)
            ? interaction.bottomRightHold
            : interaction.bottomRightTap;
        props.slots[0] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(interaction.bottomLeftHold),
            deleteHoldActive ? Visual::ARMED : (canDeleteSelection ? Visual::ACTIVE : Visual::DISABLED),
            Tone::DESTRUCTIVE
        );
        props.slots[0].holdActive = deleteHoldActive;
        props.slots[0].holdStartedAtMs = holdState.startedAtMs.get();
        props.slots[0].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
        props.slots[1] = SlotProps{
            .visualState = Visual::ACTIVE,
            .tone = Tone::NEUTRAL,
            .showIcon = false,
            .icon = nullptr,
            .showLabel = true,
        };
        formatSelectionLabel(
            props.slots[1].labelText,
            countSelectedItems(selectionMask)
        );
        props.slots[2] = core::ui::makeStandaloneIconStripSlot(
            interactionActionIcon(rightAction),
            pasteHoldActive
                ? Visual::ARMED
                : ((canCopySelection || canPasteSelection) ? Visual::ACTIVE : Visual::DISABLED),
            pasteHoldActive ? Tone::POSITIVE : Tone::NEUTRAL
        );
        props.slots[2].holdActive = pasteHoldActive;
        props.slots[2].holdStartedAtMs = holdState.startedAtMs.get();
        props.slots[2].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
        return props;
    }

    const bool canClear =
        interaction.bottomLeftTap != InteractionAction::NONE &&
        bottomContext.currentStructureCanClear;
    const bool canRemove =
        interaction.bottomLeftHold != InteractionAction::NONE &&
        bottomContext.currentStructureCanRemove;
    const bool canCopy =
        interaction.bottomRightTap != InteractionAction::NONE &&
        bottomContext.currentStructureCanCopy;
    const bool canPaste =
        interaction.bottomRightHold != InteractionAction::NONE &&
        bottomContext.compatibleClipboardAvailable;
    const bool pasteOverwritesDestination = canPaste && !bottomContext.previewingAddSlot;
    const bool copyOrPasteAvailable = canCopy || canPaste;
    const auto& holdState = trackFocus ? source.trackNavigation.hold : source.sequencer.structureUi.pageHold;
    const auto holdAction = holdState.action.get();
    const bool removeHoldActive =
        holdAction == core::state::StructureHoldAction::REMOVE && canRemove;
    const bool pasteHoldActive =
        holdAction == core::state::StructureHoldAction::PASTE && canPaste;
    const auto leftAction = removeHoldActive
        ? interaction.bottomLeftHold
        : interaction.bottomLeftTap;
    const auto rightAction = (pasteHoldActive || (!canCopy && canPaste))
        ? interaction.bottomRightHold
        : interaction.bottomRightTap;

    props.slots[0] = core::ui::makeStandaloneIconStripSlot(
        interactionActionIcon(leftAction),
        leftAction == InteractionAction::NONE
            ? Visual::HIDDEN
            : (removeHoldActive
                   ? Visual::ARMED
                   : ((canClear || canRemove) ? Visual::ACTIVE : Visual::HIDDEN)),
        removeHoldActive ? Tone::DESTRUCTIVE : Tone::WARNING
    );
    props.slots[0].holdActive = removeHoldActive;
    props.slots[0].holdStartedAtMs = holdState.startedAtMs.get();
    props.slots[0].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    props.slots[1].visualState = Visual::HIDDEN;
    props.slots[2] = core::ui::makeStandaloneIconStripSlot(
        interactionActionIcon(rightAction),
        rightAction == InteractionAction::NONE
            ? Visual::HIDDEN
            : (pasteHoldActive
                   ? Visual::ARMED
                   : (copyOrPasteAvailable ? Visual::ACTIVE : Visual::DISABLED)),
        pasteHoldActive
            ? (pasteOverwritesDestination ? Tone::WARNING : Tone::POSITIVE)
            : Tone::NEUTRAL
    );
    props.slots[2].holdActive = pasteHoldActive;
    props.slots[2].holdStartedAtMs = holdState.startedAtMs.get();
    props.slots[2].holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
    return props;
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

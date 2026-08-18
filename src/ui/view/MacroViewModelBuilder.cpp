#include "ui/view/MacroViewModelBuilder.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include "config/Timing.hpp"
#include "state/StructureSelectionInteractionPolicy.hpp"
#include "state/macro/MacroInteractionContextBuilder.hpp"
#include "state/macro/MacroInteractionPolicy.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "ui/font/StandaloneIcons.hpp"
#include "ui/theme/StandaloneTheme.hpp"

namespace core::ui {

namespace structure_slots = core::state::shared;

namespace {

uint8_t nextAddIndexOrCount(uint16_t enabledMask, uint8_t count) {
    const int next = structure_slots::nextAddIndexAfterHighest(enabledMask, count);
    return next >= 0 ? static_cast<uint8_t>(next) : count;
}

uint8_t bitCount(uint16_t mask) {
    uint8_t count = 0U;
    while (mask != 0U) {
        count = static_cast<uint8_t>(count + (mask & 1U));
        mask = static_cast<uint16_t>(mask >> 1U);
    }
    return count;
}

ContextActionStripVisualState selectionVisual(
    core::state::StructureSelectionInteractionVisibility visibility
) {
    using SelectionVisibility =
        core::state::StructureSelectionInteractionVisibility;
    switch (visibility) {
        case SelectionVisibility::ACTIVE:
            return ContextActionStripVisualState::ACTIVE;
        case SelectionVisibility::DISABLED:
            return ContextActionStripVisualState::DISABLED;
        case SelectionVisibility::HIDDEN:
        default:
            return ContextActionStripVisualState::HIDDEN;
    }
}

uint8_t clampTrackPreviewIndex(uint8_t index) {
    return static_cast<uint8_t>(std::min<uint16_t>(index, core::state::macro::TRACK_COUNT - 1U));
}

uint8_t clampPagePreviewIndex(uint8_t index) {
    return static_cast<uint8_t>(std::min<uint16_t>(index, core::state::macro::PAGE_COUNT - 1U));
}

struct MacroDisplayContext {
    core::state::StructureNavigationFocus focus =
        core::state::StructureNavigationFocus::STEP;
    uint8_t activeTrack = 0U;
    uint8_t displayTrack = 0U;
    uint8_t displayPage = 0U;
    uint8_t addTrackIndex = core::state::macro::TRACK_COUNT;
    uint8_t addPageIndex = core::state::macro::PAGE_COUNT;
    bool focusingTrack = false;
    bool focusingPage = false;
    bool slotSelectionActive = false;
    bool pageSelectionActive = false;
    bool previewTrackAddSlot = false;
    bool previewPageAddSlot = false;
    bool trackExists = false;
    bool pageExists = false;
};

FLASHMEM MacroDisplayContext macroDisplayContext(
    const MacroViewModelSource& source
) {
    MacroDisplayContext context;
    context.focus = core::state::macro::effectiveMacroNavigationFocus(
        source.navigationFocus.get()
    );
    context.focusingTrack =
        context.focus == core::state::StructureNavigationFocus::TRACK;
    context.focusingPage =
        context.focus == core::state::StructureNavigationFocus::PAGE;
    context.slotSelectionActive = source.macroUi.slotSelection.active.get();
    context.pageSelectionActive = source.macroUi.pageSelection.active.get();

    const bool previewAddTrackSlot =
        source.trackNavigation.previewAddSlot.get();
    context.activeTrack = source.sharedTrackActive.get();
    context.addTrackIndex = previewAddTrackSlot
        ? clampTrackPreviewIndex(
              source.trackNavigation.previewTrackIndex.get()
          )
        : nextAddIndexOrCount(
              source.sharedTrackEnabledMask.get(),
              core::state::macro::TRACK_COUNT
          );
    const uint8_t previewTrackIndex = clampTrackPreviewIndex(
        source.trackNavigation.previewTrackIndex.get()
    );
    context.previewTrackAddSlot =
        previewAddTrackSlot && context.focusingTrack &&
        context.addTrackIndex < core::state::macro::TRACK_COUNT;
    context.displayTrack =
        (context.focusingTrack || context.previewTrackAddSlot)
        ? previewTrackIndex
        : context.activeTrack;
    context.trackExists =
        (source.sharedTrackEnabledMask.get() &
         static_cast<uint16_t>(1U << context.displayTrack)) != 0U;

    const auto& displayTrackData =
        source.pages.tracks[context.displayTrack];
    context.addPageIndex = nextAddIndexOrCount(
        displayTrackData.enabledPageMask,
        core::state::macro::PAGE_COUNT
    );
    const uint8_t selectionPage = static_cast<uint8_t>(
        source.macroUi.slotSelection.cursorLinear.get() /
        core::state::macro::MACRO_COUNT
    );
    const bool previewAddPageSlot =
        source.macroUi.previewAddPageSlot.get();
    const uint8_t previewPageIndex = context.slotSelectionActive
        ? clampPagePreviewIndex(selectionPage)
        : context.pageSelectionActive
            ? clampPagePreviewIndex(
                  source.macroUi.pageSelection.cursorIndex.get()
              )
            : (previewAddPageSlot && context.focusingPage &&
               context.addPageIndex < core::state::macro::PAGE_COUNT)
                ? context.addPageIndex
                : clampPagePreviewIndex(
                      source.macroUi.previewPageIndex.get()
                  );
    context.previewPageAddSlot = context.slotSelectionActive
        ? !displayTrackData.isPageEnabled(previewPageIndex)
        : context.pageSelectionActive
            ? !displayTrackData.isPageEnabled(previewPageIndex)
            : previewAddPageSlot && context.focusingPage &&
              context.addPageIndex < core::state::macro::PAGE_COUNT;
    context.displayPage =
        context.slotSelectionActive || context.pageSelectionActive
        ? previewPageIndex
        : (context.focusingPage || context.previewPageAddSlot)
            ? previewPageIndex
            : displayTrackData.activePage;
    context.pageExists =
        context.trackExists &&
        displayTrackData.isPageEnabled(context.displayPage);
    return context;
}

FLASHMEM core::state::macro::MacroInteractionContext macroInteractionContext(
    const MacroViewModelSource& source
) {
    return core::state::macro::buildMacroInteractionContext(
        core::state::macro::MacroInteractionContextSource{
            .pages = source.pages,
            .macroUi = source.macroUi,
            .trackNavigation = source.trackNavigation,
            .structureClipboard = source.structureClipboard,
            .navigationFocus = source.navigationFocus.get(),
            .enabledTrackMask = source.sharedTrackEnabledMask.get(),
            .blockingOverlay = false,
            .slotPropertySelecting =
                source.macroUi.performanceOverlayMode.get() !=
                core::state::macro::MacroPerformanceOverlayMode::NONE,
        }
    );
}

ContextActionStripVisualState macroVisual(
    core::state::macro::MacroInteractionVisibility visibility
) {
    switch (visibility) {
        case core::state::macro::MacroInteractionVisibility::ACTIVE:
            return ContextActionStripVisualState::ACTIVE;
        case core::state::macro::MacroInteractionVisibility::DISABLED:
            return ContextActionStripVisualState::DISABLED;
        case core::state::macro::MacroInteractionVisibility::DIM:
            return ContextActionStripVisualState::DIM;
        case core::state::macro::MacroInteractionVisibility::HIDDEN:
        default:
            return ContextActionStripVisualState::HIDDEN;
    }
}

}  // namespace

FLASHMEM MacroHeaderBarProps buildMacroHeaderBarProps(const MacroViewModelSource& source) {
    OC_PERF_SCOPE(perfProjection, "ui.macro.projection.header");
    MacroHeaderBarProps props;
    const auto context = macroDisplayContext(source);
    const auto& displayTrackData = source.pages.tracks[context.displayTrack];

    props.activeTrack = context.activeTrack;
    props.previewTrack = context.displayTrack;
    props.activePage = source.pages.currentActivePage();
    props.previewPage = context.displayPage;
    props.addPageIndex = context.addPageIndex;
    props.addTrackIndex = context.addTrackIndex;
    props.enabledMask = context.trackExists
        ? displayTrackData.enabledPageMask
        : 0U;
    props.trackEnabledMask = source.sharedTrackEnabledMask.get();
    props.performanceOverlayMode = source.macroUi.performanceOverlayMode.get();
    props.automationTakePhase = source.macroUi.automationTake.phase;
    props.automationTakeTiming = source.macroUi.automationTake.timing;
    props.automationTakeTouchedMask = source.macroUi.automationTake.touchedMask;
    props.focusingPage = context.focusingPage;
    props.focusingTrack = context.focusingTrack;
    props.slotSelectionActive = context.slotSelectionActive;
    props.pageSelectionActive = context.pageSelectionActive;
    props.previewPageAddSlot = context.previewPageAddSlot;
    props.previewTrackAddSlot = context.previewTrackAddSlot;
    props.automationRecordingStatus = source.macroUi.automationRecordingStatus.get();
    if (context.pageSelectionActive) {
        const auto& selection = source.macroUi.pageSelection;
        props.pageSelectedMask = selection.selectedMask.get();
        if (selection.placing.get()) {
            props.pageDestinationMask =
                selection.destinationMask.get();
            props.pageOverwriteMask =
                selection.overwriteMask.get();
            props.pageBlockedMask =
                selection.pasteBlocked.get()
                    ? props.pageDestinationMask
                    : 0U;
        }
    }

    props.pageOutputActivity.fill(0);
    if (context.displayTrack == context.activeTrack &&
        context.trackExists && source.statusBar.ccOutActive.get()) {
        props.pageOutputActivity[displayTrackData.activePage] = 127;
    }

    return props;
}

FLASHMEM StepPropertySelectionOverlayProps buildMacroSlotPropertyOverlayProps(
    const MacroViewModelSource& source
) {
    OC_PERF_SCOPE(perfProjection, "ui.macro.projection.slot-overlay");
    if (source.macroUi.contextSelector.visible) {
        const auto focus = source.macroUi.contextSelector.previewFocus;
        const bool track = focus == core::state::StructureNavigationFocus::TRACK;
        const bool page = focus == core::state::StructureNavigationFocus::PAGE;
        StepPropertySelectionOverlayProps props{
            .visible = true,
            .customContent = true,
            .icon = track
                ? standalone::icons::TRACK_MUTE
                : page ? standalone::icons::LENGTH : standalone::icons::KNOB,
            .label = track ? "Track" : page ? "Page" : "Macro",
            .useValueText = true,
            .color = standalone::theme::color::MACRO_CC_COLOR,
        };
        std::snprintf(
            props.valueText.data(),
            props.valueText.size(),
            "Turn + release"
        );
        return props;
    }

    const auto mode = source.macroUi.performanceOverlayMode.get();
    if (mode != core::state::macro::MacroPerformanceOverlayMode::EDIT) {
        return {.visible = false};
    }

    StepPropertySelectionOverlayProps props{
        .visible = true,
        .customContent = true,
        .useValueText = true,
    };

    props.icon = standalone::icons::KNOB;
    props.label = "Edit";
    props.color = standalone::theme::color::MACRO_CC_COLOR;
    std::snprintf(
        props.valueText.data(),
        props.valueText.size(),
        "Press a macro"
    );
    return props;
}

FLASHMEM ContextActionStripProps buildMacroLeftActionStripProps(const MacroViewModelSource& source) {
    OC_PERF_SCOPE(perfProjection, "ui.macro.projection.left-actions");
    using Visual = ContextActionStripVisualState;
    using Tone = ContextActionStripTone;

    ContextActionStripProps props;
    props.visible = true;

    if (source.macroUi.slotSelection.active.get() ||
        source.macroUi.pageSelection.active.get() ||
        source.trackNavigation.selection.active.get()) {
        for (auto& slot : props.slots) {
            slot.visualState = Visual::HIDDEN;
        }
        return props;
    }

    const auto performanceMode = source.macroUi.performanceOverlayMode.get();
    props.slots[0].visualState = Visual::HIDDEN;
    props.slots[1] = {
        .visualState = performanceMode ==
                core::state::macro::MacroPerformanceOverlayMode::AUTOMATION_TAKE
            ? Visual::PRESSED
            : Visual::ACTIVE,
        .tone = Tone::WARNING,
        .showIcon = true,
        .icon = standalone::icons::AUTOMATION,
    };
    props.slots[2] = {
        .visualState = performanceMode ==
                core::state::macro::MacroPerformanceOverlayMode::EDIT
            ? Visual::PRESSED
            : Visual::ACTIVE,
        .tone = Tone::NEUTRAL,
        .showIcon = true,
        .icon = standalone::icons::KNOB,
    };

    return props;
}

FLASHMEM ContextActionStripProps buildMacroBottomActionStripProps(const MacroViewModelSource& source) {
    OC_PERF_SCOPE(perfProjection, "ui.macro.projection.bottom-actions");
    ContextActionStripProps props;
    props.visible = true;

    const bool slotSelecting =
        source.macroUi.slotSelection.active.get();
    const bool pageSelecting =
        source.macroUi.pageSelection.active.get();
    const bool trackSelecting =
        source.trackNavigation.selection.active.get();
    if (slotSelecting || pageSelecting || trackSelecting) {
        props.slots[0].visualState =
            ContextActionStripVisualState::HIDDEN;
        props.slots[1].visualState =
            ContextActionStripVisualState::HIDDEN;
        auto& action = props.slots[2];
        const bool placing = slotSelecting
            ? source.macroUi.slotSelection.placing.get()
            : pageSelecting
                ? source.macroUi.pageSelection.placing.get()
                : source.trackNavigation.selection.placing.get();
        const bool blocked = slotSelecting
            ? source.macroUi.slotSelection.pasteBlocked
            : pageSelecting
                ? source.macroUi.pageSelection.pasteBlocked.get()
                : source.trackNavigation.selection.pasteBlocked.get();
        const uint16_t selectedMask = pageSelecting
            ? source.macroUi.pageSelection.selectedMask.get()
            : trackSelecting
                ? source.trackNavigation.selection.selectedMask.get()
                : 0U;
        const uint16_t overwriteMask = pageSelecting
            ? source.macroUi.pageSelection.overwriteMask.get()
            : trackSelecting
                ? source.trackNavigation.selection.overwriteMask.get()
                : 0U;
        const uint8_t selectedCount = slotSelecting
            ? source.macroUi.slotSelection.selectedCount()
            : bitCount(selectedMask);
        const uint8_t overwriteCount = slotSelecting
            ? source.macroUi.slotSelection.overwriteCount
            : bitCount(overwriteMask);
        bool destinationAvailable = !placing;
        if (placing && slotSelecting) {
            destinationAvailable = false;
            for (const uint8_t mask :
                 source.macroUi.slotSelection.destinationMasks) {
                if (mask == 0U) continue;
                destinationAvailable = true;
                break;
            }
        } else if (placing) {
            destinationAvailable = pageSelecting
                ? source.macroUi.pageSelection.destinationMask.get() != 0U
                : source.trackNavigation.selection.destinationMask.get() != 0U;
        }
        const bool pasteAvailable =
            placing && !blocked && destinationAvailable;
        const auto selectionPolicy =
            core::state::buildStructureSelectionInteractionPolicy({
                .entryAvailable = false,
                .active = true,
                .placing = placing,
                .selectedItemsAvailable = selectedCount > 0U,
                .pasteAvailable = pasteAvailable,
            });
        const bool overwrite = overwriteCount > 0U;
        const bool pasteHold =
            source.macroUi.pageHold.action.get() ==
                core::state::StructureHoldAction::PASTE &&
            selectionPolicy.bottomRightLongPress ==
                core::state::StructureSelectionInteractionAction::
                    PASTE_SELECTION;
        action.visualState = pasteHold
            ? ContextActionStripVisualState::ARMED
            : selectionVisual(selectionPolicy.bottomRightVisibility);
        action.tone = placing && !pasteAvailable
            ? ContextActionStripTone::DESTRUCTIVE
            : overwrite
                ? ContextActionStripTone::WARNING
                : ContextActionStripTone::CONSTRUCTIVE;
        action.showLabel = true;
        if (!placing) {
            std::snprintf(
                action.labelText.data(),
                action.labelText.size(),
                "CPY \xC2\xB7 %u",
                static_cast<unsigned>(selectedCount)
            );
        } else if (overwrite) {
            std::snprintf(
                action.labelText.data(),
                action.labelText.size(),
                "PST \xC2\xB7 %u OVR",
                static_cast<unsigned>(overwriteCount)
            );
        } else {
            std::snprintf(
                action.labelText.data(),
                action.labelText.size(),
                "%s",
                pasteAvailable ? "PST" : "PST BLOCK"
            );
        }
        action.holdActive = pasteHold;
        action.holdStartedAtMs =
            source.macroUi.pageHold.startedAtMs.get();
        action.holdDurationMs =
            Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS;
        return props;
    }

    const auto context = macroInteractionContext(source);
    const bool trackFocus =
        context.navigationFocus == core::state::StructureNavigationFocus::TRACK;
    const auto policy = core::state::macro::MacroInteractionPolicy::actionStrip(context);
    const bool canPaste = context.compatibleClipboardAvailable;
    const auto& holdState = trackFocus ? source.trackNavigation.hold : source.macroUi.pageHold;
    const auto holdAction = holdState.action.get();
    const bool removeHoldActive = holdAction == core::state::StructureHoldAction::REMOVE;
    const bool pasteHoldActive = holdAction == core::state::StructureHoldAction::PASTE;

    props.slots[0] = {
        .visualState = removeHoldActive
            ? ContextActionStripVisualState::ARMED
            : macroVisual(policy.bottomLeft),
        .tone = ContextActionStripTone::DESTRUCTIVE,
        .showIcon = true,
        .icon = removeHoldActive ? standalone::icons::ACTION_CANCEL
                                 : standalone::icons::ACTION_REMOVE,
        .holdActive = removeHoldActive,
        .holdStartedAtMs = holdState.startedAtMs.get(),
        .holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS,
    };
    props.slots[1].visualState = ContextActionStripVisualState::HIDDEN;
    props.slots[2] = {
        .visualState = pasteHoldActive
            ? ContextActionStripVisualState::ARMED
            : (canPaste
            ? ContextActionStripVisualState::ARMED
            : macroVisual(policy.bottomRight)),
        .tone = canPaste ? ContextActionStripTone::CONSTRUCTIVE : ContextActionStripTone::NEUTRAL,
        .showIcon = true,
        .icon = canPaste ? standalone::icons::ACTION_PASTE : standalone::icons::ACTION_COPY,
        .holdActive = pasteHoldActive,
        .holdStartedAtMs = holdState.startedAtMs.get(),
        .holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS,
    };
    return props;
}

namespace {

FLASHMEM MacroWidgetProps buildMacroWidgetPropsForDisplay(
    const MacroViewModelSource& source,
    const MacroDisplayContext& display,
    uint8_t index
) {
    if (index >= Config::MACRO_COUNT) return {};

    const bool selectionActive =
        source.macroUi.slotSelection.active.get();
    const bool placementActive =
        selectionActive &&
        source.macroUi.slotSelection.placing.get();
    const uint8_t displayTrack = display.displayTrack;
    const uint8_t displayPage = display.displayPage;
    const bool pageExists = display.pageExists;
    const auto& page =
        source.pages.pageData(displayTrack, displayPage);
    const bool active =
        pageExists && page.isMacroActive(index);
    const bool addSlot =
        !selectionActive && pageExists && !active;
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = displayTrack,
        .page = displayPage,
        .macro = index,
    };
    core::state::modulation::ProjectControlMacroDestinationView controlSlot{};
    const bool controlSlotValid =
        core::state::modulation::readProjectControlMacroDestination(
            source.pages.control,
            address,
            controlSlot
        );
    const uint16_t overrideBit = static_cast<uint16_t>(1U << index);
    const bool manualOverride =
        pageExists &&
        displayTrack == source.pages.currentActiveTrack() &&
        displayPage == source.pages.currentActivePage() &&
        (source.macroUi.automationManualOverrideMask.get() & overrideBit) != 0;
    const uint8_t cursorLinear =
        source.macroUi.slotSelection.cursorLinear.get();
    const bool focused = selectionActive
        ? cursorLinear ==
              static_cast<uint8_t>(
                  displayPage *
                      core::state::macro::MACRO_COUNT +
                  index
              )
        : display.focus == core::state::StructureNavigationFocus::STEP &&
          source.macroUi.focusedMacroSlot.get() == index;
    const bool recording =
        pageExists &&
        source.macroUi.automationTake.phase ==
            core::state::macro::MacroAutomationTakePhase::RECORDING &&
        source.macroUi.automationTake.track == displayTrack &&
        source.macroUi.automationTake.page == displayPage &&
        source.macroUi.automationTake.activeFor(index);
    const bool automationStored =
        controlSlotValid && controlSlot.automation.stored();
    const bool modulationStored =
        controlSlotValid && controlSlot.modulationCount > 0U;
    const bool automationPlayback =
        automationStored && controlSlot.automation.enabled;
    const bool modulationPlayback =
        modulationStored && controlSlot.activeModulationCount > 0U;
    const bool modulationPaused =
        modulationPlayback && controlSlot.modulationCount == 1U &&
        controlSlot.primaryModulation.amount == 0.0f;
    const auto& projection = source.macroUi.runtimeProjections[index];
    const bool projectionValid =
        source.macroUi.runtimeProjectionValidFor(
            address.track,
            address.page,
            index
        );
    // Runtime signals describe only the active Page. While Selection previews
    // another Page, use that Page's durable Base instead of leaking the active
    // Page value into the preview.
    const bool displayingActivePage =
        pageExists &&
        displayTrack == source.pages.currentActiveTrack() &&
        displayPage == source.pages.currentActivePage();
    const float fallbackValue = !pageExists
        ? 0.5f
        : displayingActivePage
            ? source.macros.slots[index].value.get()
            : page.values[index];
    const float projectedBase = projectionValid
        ? projection.base
        : fallbackValue;
    const float projectedModulation = projectionValid
        ? projection.modulation
        : 0.0f;
    // A physical Record gesture is newer than the last periodic runtime
    // frame. Prioritize its Base immediately while retaining the current
    // relative Modulation contribution; otherwise the ring appears frozen
    // until the next playback evaluation.
    const float visibleBase = recording
        ? source.macroUi.automationTake.latestBase(index)
        : projectedBase;
    const float visibleUnclamped = visibleBase + projectedModulation;
    const float visibleResolved = recording
        ? core::state::macro::macroAutomationClamp01(visibleUnclamped)
        : (projectionValid ? projection.resolved : fallbackValue);
    const uint8_t slotBit = static_cast<uint8_t>(1U << index);
    const uint8_t sourceLinear = static_cast<uint8_t>(
        displayPage * core::state::macro::MACRO_COUNT + index
    );
    const bool selected = selectionActive &&
        source.macroUi.slotSelection.selected(sourceLinear);
    MacroSlotPlacementPreview placementPreview =
        MacroSlotPlacementPreview::NONE;
    if (placementActive &&
        (source.macroUi.slotSelection.destinationMasks[displayPage] &
         slotBit) != 0U) {
        if (source.macroUi.slotSelection.pasteBlocked) {
            placementPreview =
                MacroSlotPlacementPreview::BLOCKED;
        } else if (
            (source.macroUi.slotSelection.overwriteMasks[displayPage] &
             slotBit) != 0U) {
            placementPreview =
                MacroSlotPlacementPreview::OVERWRITE;
        } else {
            placementPreview =
                MacroSlotPlacementPreview::FREE;
        }
    }
    return {
        .value = visibleResolved,
        .baseValue = visibleBase,
        .modulationDelta = projectedModulation,
        .modulationDepth = controlSlotValid
            ? controlSlot.primaryModulation.amount
            : 0.0f,
        .modulationSourceCount = static_cast<uint8_t>(
            controlSlotValid
                ? std::min<uint16_t>(controlSlot.modulationCount, 7U)
                : 0U
        ),
        .cc = page.cc[index],
        .automationStored = active && automationStored,
        .automationActive = active && automationPlayback && !manualOverride,
        .modulationStored = active && modulationStored,
        .modulationActive = active && modulationPlayback &&
            !modulationPaused,
        .modulationPaused = active && modulationPaused,
        .automationRecording = active && recording,
        .automationManualOverride = active && manualOverride,
        .clippedLow = recording
            ? visibleUnclamped < 0.0f
            : (projectionValid && projection.clippedLow),
        .clippedHigh = recording
            ? visibleUnclamped > 1.0f
            : (projectionValid && projection.clippedHigh),
        .active = active,
        .addSlot = addSlot,
        .focused = focused,
        .selected = selected,
        .placementPreview = placementPreview,
    };
}

}  // namespace

FLASHMEM MacroWidgetProps buildMacroWidgetProps(
    const MacroViewModelSource& source,
    uint8_t index
) {
    return buildMacroWidgetPropsForDisplay(
        source,
        macroDisplayContext(source),
        index
    );
}

FLASHMEM MacroViewFrameState buildMacroViewFrameState(const MacroViewModelSource& source) {
    OC_PERF_SCOPE(perfProjection, "ui.macro.projection.frame");
    MacroViewFrameState frame;
    const auto display = macroDisplayContext(source);
    for (uint8_t index = 0; index < Config::MACRO_COUNT; ++index) {
        frame.macros[index] = buildMacroWidgetPropsForDisplay(
            source,
            display,
            index
        );
    }
    return frame;
}

}  // namespace core::ui

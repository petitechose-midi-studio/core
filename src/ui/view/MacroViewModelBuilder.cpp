#include "ui/view/MacroViewModelBuilder.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>
#include <oc/diagnostics/Performance.hpp>
#include "config/Timing.hpp"
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

uint8_t clampTrackPreviewIndex(uint8_t index) {
    return static_cast<uint8_t>(std::min<uint16_t>(index, core::state::macro::TRACK_COUNT - 1U));
}

uint8_t clampPagePreviewIndex(uint8_t index) {
    return static_cast<uint8_t>(std::min<uint16_t>(index, core::state::macro::PAGE_COUNT - 1U));
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
    const auto focus = core::state::macro::effectiveMacroNavigationFocus(
        source.navigationFocus.get()
    );
    const bool focusingTrack =
        focus == core::state::StructureNavigationFocus::TRACK;
    const bool focusingPage =
        focus == core::state::StructureNavigationFocus::PAGE;
    const bool slotSelectionActive =
        source.macroUi.slotSelection.active.get();
    const bool pageSelectionActive =
        source.macroUi.pageSelection.active.get();
    const bool previewAddTrackSlot = source.trackNavigation.previewAddSlot.get();
    const bool previewAddPageSlot = source.macroUi.previewAddPageSlot.get();
    const uint8_t activeTrack = source.sharedTrackActive.get();
    const uint8_t addTrackIndex =
        previewAddTrackSlot
            ? clampTrackPreviewIndex(source.trackNavigation.previewTrackIndex.get())
            : nextAddIndexOrCount(source.sharedTrackEnabledMask.get(), core::state::macro::TRACK_COUNT);
    const uint8_t previewTrackIndex =
        clampTrackPreviewIndex(source.trackNavigation.previewTrackIndex.get());
    const bool previewTrackAddSlot =
        previewAddTrackSlot &&
        focus == core::state::StructureNavigationFocus::TRACK &&
        addTrackIndex < core::state::macro::TRACK_COUNT;
    const uint8_t displayTrack =
        (focusingTrack || previewTrackAddSlot) ? previewTrackIndex : activeTrack;
    const auto& displayTrackData = source.pages.tracks[displayTrack];
    const uint8_t addPageIndex = nextAddIndexOrCount(
        displayTrackData.enabledPageMask,
        core::state::macro::PAGE_COUNT
    );
    const uint8_t selectionPage = static_cast<uint8_t>(
        source.macroUi.slotSelection.cursorLinear.get() /
        core::state::macro::MACRO_COUNT
    );
    const uint8_t previewPageIndex = slotSelectionActive
        ? clampPagePreviewIndex(selectionPage)
        : pageSelectionActive
            ? clampPagePreviewIndex(
                  source.macroUi.pageSelection.cursorIndex.get()
              )
        : (previewAddPageSlot &&
         focus == core::state::StructureNavigationFocus::PAGE &&
         addPageIndex < core::state::macro::PAGE_COUNT)
            ? addPageIndex
            : clampPagePreviewIndex(source.macroUi.previewPageIndex.get());
    const bool previewPageAddSlot = slotSelectionActive
        ? !displayTrackData.isPageEnabled(previewPageIndex)
        : pageSelectionActive
            ? !displayTrackData.isPageEnabled(previewPageIndex)
        : previewAddPageSlot &&
          focus == core::state::StructureNavigationFocus::PAGE &&
          addPageIndex < core::state::macro::PAGE_COUNT;
    const uint8_t displayPage =
        slotSelectionActive || pageSelectionActive
        ? previewPageIndex
        : (focusingPage || previewPageAddSlot)
            ? previewPageIndex
            : displayTrackData.activePage;

    props.activeTrack = activeTrack;
    props.previewTrack = displayTrack;
    props.activePage = source.pages.currentActivePage();
    props.previewPage = displayPage;
    props.addPageIndex = addPageIndex;
    props.addTrackIndex = addTrackIndex;
    props.enabledMask = displayTrackData.enabledPageMask;
    props.trackEnabledMask = source.sharedTrackEnabledMask.get();
    props.performanceOverlayMode = source.macroUi.performanceOverlayMode.get();
    props.automationTakePhase = source.macroUi.automationTake.phase;
    props.automationTakeTiming = source.macroUi.automationTake.timing;
    props.automationTakeTouchedMask = source.macroUi.automationTake.touchedMask;
    props.focusingPage = focusingPage;
    props.focusingTrack = focusingTrack;
    props.slotSelectionActive = slotSelectionActive;
    props.pageSelectionActive = pageSelectionActive;
    props.previewPageAddSlot = previewPageAddSlot;
    props.previewTrackAddSlot = previewTrackAddSlot;
    props.automationRecordingStatus = source.macroUi.automationRecordingStatus.get();
    if (pageSelectionActive) {
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
    if (source.statusBar.ccOutActive.get()) {
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
            .label = track ? "TRACK" : page ? "PAGE" : "MACRO",
            .useValueText = true,
            .color = standalone::theme::color::MACRO_CC_COLOR,
        };
        std::snprintf(
            props.valueText.data(),
            props.valueText.size(),
            "TURN + RELEASE"
        );
        return props;
    }

    const auto mode = source.macroUi.performanceOverlayMode.get();
    if (mode == core::state::macro::MacroPerformanceOverlayMode::NONE) {
        return {.visible = false};
    }

    StepPropertySelectionOverlayProps props{
        .visible = true,
        .customContent = true,
        .useValueText = true,
    };

    if (mode == core::state::macro::MacroPerformanceOverlayMode::EDIT) {
        props.icon = standalone::icons::KNOB;
        props.label = "EDIT";
        props.color = standalone::theme::color::MACRO_CC_COLOR;
        std::snprintf(
            props.valueText.data(),
            props.valueText.size(),
            "PRESS A MACRO"
        );
        return props;
    }

    props.icon = standalone::icons::MACRO_AUTOMATION;
    props.label = source.macroUi.automationTake.phase ==
            core::state::macro::MacroAutomationTakePhase::RECORDING
        ? "RECORDING"
        : "AUTOMATION TAKE";
    props.color = standalone::theme::color::MACRO_AUTOMATION;
    if (source.macroUi.automationTake.phase ==
        core::state::macro::MacroAutomationTakePhase::RECORDING) {
        uint8_t count = 0U;
        uint16_t mask = source.macroUi.automationTake.touchedMask;
        while (mask != 0U) {
            count = static_cast<uint8_t>(count + (mask & 1U));
            mask = static_cast<uint16_t>(mask >> 1U);
        }
        std::snprintf(
            props.valueText.data(),
            props.valueText.size(),
            "%u MACRO%s",
            static_cast<unsigned>(count),
            count == 1U ? "" : "S"
        );
    } else {
        std::snprintf(
            props.valueText.data(),
            props.valueText.size(),
            "%s",
            core::state::macro::macroAutomationTakeTimingLabel(
                source.macroUi.automationTake.timing
            )
        );
    }
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
        .icon = standalone::icons::MACRO_AUTOMATION,
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
        const bool overwrite = overwriteCount > 0U;
        const bool pasteHold =
            source.macroUi.pageHold.action.get() ==
                core::state::StructureHoldAction::PASTE;
        action.visualState = pasteHold
            ? ContextActionStripVisualState::ARMED
            : blocked
                ? ContextActionStripVisualState::DISABLED
                : ContextActionStripVisualState::ACTIVE;
        action.tone = blocked
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
                blocked ? "PST BLOCK" : "PST"
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

FLASHMEM MacroWidgetProps buildMacroWidgetProps(
    const MacroViewModelSource& source,
    uint8_t index
) {
    OC_PERF_SCOPE(perfProjection, "ui.macro.projection.knob");
    OC_PERF_UNITS(perfProjection, index, 0U);
    if (index >= Config::MACRO_COUNT) return {};

    const auto focus = core::state::macro::effectiveMacroNavigationFocus(
        source.navigationFocus.get()
    );
    const bool selectionActive =
        source.macroUi.slotSelection.active.get();
    const bool placementActive =
        selectionActive &&
        source.macroUi.slotSelection.placing.get();
    const uint8_t displayTrack =
        source.pages.currentActiveTrack();
    const uint8_t displayPage = selectionActive
        ? static_cast<uint8_t>(
              source.macroUi.slotSelection.cursorLinear.get() /
              core::state::macro::MACRO_COUNT
          )
        : source.pages.currentActivePage();
    const bool pageExists =
        displayPage < core::state::macro::PAGE_COUNT &&
        source.pages.tracks[displayTrack].isPageEnabled(
            displayPage
        );
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
        : focus == core::state::StructureNavigationFocus::STEP &&
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

FLASHMEM MacroViewFrameState buildMacroViewFrameState(const MacroViewModelSource& source) {
    OC_PERF_SCOPE(perfProjection, "ui.macro.projection.frame");
    MacroViewFrameState frame;
    for (uint8_t index = 0; index < Config::MACRO_COUNT; ++index) {
        frame.macros[index] = buildMacroWidgetProps(source, index);
    }
    return frame;
}

}  // namespace core::ui

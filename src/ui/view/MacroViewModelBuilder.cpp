#include "ui/view/MacroViewModelBuilder.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>
#include "config/Timing.hpp"
#include "state/macro/MacroInteractionContextBuilder.hpp"
#include "state/macro/MacroInteractionPolicy.hpp"
#include "state/macro/MacroSelectionDeleteAction.hpp"
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

uint8_t clampTrackPreviewIndex(uint8_t index) {
    return static_cast<uint8_t>(std::min<uint16_t>(index, core::state::macro::TRACK_COUNT - 1U));
}

uint8_t clampPagePreviewIndex(uint8_t index) {
    return static_cast<uint8_t>(std::min<uint16_t>(index, core::state::macro::PAGE_COUNT - 1U));
}

core::state::macro::MacroInteractionContext macroInteractionContext(
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

ContextActionStripVisualState selectionDeleteVisual(
    core::state::macro::MacroSelectionDeletePresentationState state
) {
    using Presentation = core::state::macro::MacroSelectionDeletePresentationState;
    switch (state) {
        case Presentation::AVAILABLE:
            return ContextActionStripVisualState::AVAILABLE;
        case Presentation::PRESSED:
            return ContextActionStripVisualState::PRESSED;
        case Presentation::ARMED:
            return ContextActionStripVisualState::ARMED;
        case Presentation::CANCELLED:
            return ContextActionStripVisualState::CANCELLED;
        case Presentation::APPLIED:
            return ContextActionStripVisualState::APPLIED;
        case Presentation::DISABLED:
        default:
            return ContextActionStripVisualState::DISABLED;
    }
}

}  // namespace

FLASHMEM MacroHeaderBarProps buildMacroHeaderBarProps(const MacroViewModelSource& source) {
    MacroHeaderBarProps props;
    const bool focusingTrack =
        !source.trackNavigation.selection.active.get() &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const bool focusingPage =
        !source.macroUi.pageSelection.active.get() &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE;
    const bool selectingTrack =
        source.trackNavigation.selection.active.get() &&
        source.trackNavigation.selection.scope.get() == core::state::StructureSelectionScope::TRACK;
    const bool selectingPage =
        source.macroUi.pageSelection.active.get() &&
        source.macroUi.pageSelection.scope.get() == core::state::StructureSelectionScope::PAGE;
    const bool previewAddTrackSlot =
        !source.trackNavigation.selection.active.get() && source.trackNavigation.previewAddSlot.get();
    const bool previewAddPageSlot =
        !source.macroUi.pageSelection.active.get() && source.macroUi.previewAddPageSlot.get();
    const uint8_t activeTrack = source.sharedTrackActive.get();
    const uint8_t addTrackIndex =
        previewAddTrackSlot
            ? clampTrackPreviewIndex(source.trackNavigation.previewTrackIndex.get())
            : nextAddIndexOrCount(source.sharedTrackEnabledMask.get(), core::state::macro::TRACK_COUNT);
    const uint8_t previewTrackIndex =
        clampTrackPreviewIndex(source.trackNavigation.previewTrackIndex.get());
    const bool previewTrackAddSlot =
        previewAddTrackSlot &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK &&
        addTrackIndex < core::state::macro::TRACK_COUNT;
    const uint8_t displayTrack = selectingTrack
        ? source.trackNavigation.selection.cursorIndex.get()
        : ((focusingTrack || previewTrackAddSlot) ? previewTrackIndex : activeTrack);
    const auto& displayTrackData = source.pages.tracks[displayTrack];
    const uint8_t addPageIndex = nextAddIndexOrCount(
        displayTrackData.enabledPageMask,
        core::state::macro::PAGE_COUNT
    );
    const uint8_t previewPageIndex =
        (previewAddPageSlot &&
         source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE &&
         addPageIndex < core::state::macro::PAGE_COUNT)
            ? addPageIndex
            : clampPagePreviewIndex(source.macroUi.previewPageIndex.get());
    const bool previewPageAddSlot =
        previewAddPageSlot &&
        source.navigationFocus.get() == core::state::StructureNavigationFocus::PAGE &&
        addPageIndex < core::state::macro::PAGE_COUNT;
    const uint8_t displayPage = selectingPage
        ? source.macroUi.pageSelection.cursorIndex.get()
        : ((focusingPage || previewPageAddSlot) ? previewPageIndex : displayTrackData.activePage);

    props.activeTrack = activeTrack;
    props.previewTrack = displayTrack;
    props.activePage = source.pages.currentActivePage();
    props.previewPage = displayPage;
    props.addPageIndex = addPageIndex;
    props.addTrackIndex = addTrackIndex;
    props.enabledMask = displayTrackData.enabledPageMask;
    props.trackEnabledMask = source.sharedTrackEnabledMask.get();
    props.selectedPageMask =
        (source.macroUi.pageSelection.active.get() &&
         source.macroUi.pageSelection.scope.get() == core::state::StructureSelectionScope::PAGE)
            ? source.macroUi.pageSelection.selectedMask.get()
            : 0;
    props.performanceOverlayMode = source.macroUi.performanceOverlayMode.get();
    props.automationTakePhase = source.macroUi.automationTake.phase;
    props.automationTakeTiming = source.macroUi.automationTake.timing;
    props.automationTakeTouchedMask = source.macroUi.automationTake.touchedMask;
    props.focusingPage = focusingPage;
    props.focusingTrack = focusingTrack;
    props.selectingPage = selectingPage;
    props.selectingTrack = selectingTrack;
    props.previewPageAddSlot = previewPageAddSlot;
    props.previewTrackAddSlot = previewTrackAddSlot;
    props.automationRecordingStatus = source.macroUi.automationRecordingStatus.get();

    props.pageOutputActivity.fill(0);
    if (source.statusBar.ccOutActive.get()) {
        props.pageOutputActivity[displayTrackData.activePage] = 127;
    }

    return props;
}

FLASHMEM StepPropertySelectionOverlayProps buildMacroSlotPropertyOverlayProps(
    const MacroViewModelSource& source
) {
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
    using Visual = ContextActionStripVisualState;
    using Tone = ContextActionStripTone;

    ContextActionStripProps props;
    props.visible = true;

    if (source.trackNavigation.selection.active.get() || source.macroUi.pageSelection.active.get()) {
        const bool trackScope =
            source.trackNavigation.selection.active.get();
        props.slots[0] = {
            .visualState = Visual::ACTIVE,
            .tone = Tone::NEUTRAL,
            .showIcon = true,
            .icon = standalone::icons::ACTION_CANCEL
        };
        props.slots[1] = {
            .visualState = Visual::ACTIVE,
            .tone = Tone::NEUTRAL,
            .showIcon = false,
            .icon = nullptr,
            .showLabel = true,
            .label = trackScope ? "TRK" : "PG"
        };
        props.slots[2].visualState = Visual::HIDDEN;
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
    ContextActionStripProps props;
    props.visible = true;

    const auto selectionFeedback = source.macroUi.selectionDeleteFeedback.get();
    const bool appliedSelectionDeleteFeedback =
        selectionFeedback.active &&
        selectionFeedback.action == core::state::contextual::ContextActionId::REMOVE &&
        selectionFeedback.status ==
            core::state::contextual::OperationFeedbackStatus::APPLIED;
    if (source.trackNavigation.selection.active.get() ||
        source.macroUi.pageSelection.active.get() ||
        appliedSelectionDeleteFeedback) {
        const auto context = macroInteractionContext(source);
        const auto presentation = appliedSelectionDeleteFeedback
            ? core::state::macro::MacroSelectionDeletePresentationState::APPLIED
            : core::state::macro::macroSelectionDeletePresentationState(
                context.selectionDeleteAction,
                source.macroUi.selectionDeleteGuard.get(),
                selectionFeedback
            );
        const bool armed = presentation ==
            core::state::macro::MacroSelectionDeletePresentationState::ARMED;
        const bool cancelled = presentation ==
            core::state::macro::MacroSelectionDeletePresentationState::CANCELLED;
        const bool applied = presentation ==
            core::state::macro::MacroSelectionDeletePresentationState::APPLIED;
        props.slots[0] = {
            .visualState = selectionDeleteVisual(presentation),
            .tone = applied ? ContextActionStripTone::POSITIVE
                            : (cancelled ? ContextActionStripTone::WARNING
                                         : ContextActionStripTone::DESTRUCTIVE),
            .showIcon = true,
            .icon = applied ? standalone::icons::ACTION_VALIDATE
                            : (cancelled ? standalone::icons::ACTION_CANCEL
                                         : standalone::icons::ACTION_REMOVE),
            .holdActive = armed,
            .holdStartedAtMs = source.macroUi.selectionDeleteGuard.get().pressedAtMs,
            .holdDurationMs = context.selectionDeleteAction.guard.durationMs,
        };
        props.slots[1].visualState = ContextActionStripVisualState::HIDDEN;
        if (appliedSelectionDeleteFeedback) {
            props.slots[2].visualState = ContextActionStripVisualState::HIDDEN;
        } else {
            props.slots[2] = {
                .visualState = ContextActionStripVisualState::ACTIVE,
                .tone = ContextActionStripTone::CONSTRUCTIVE,
                .showIcon = true,
                .icon = standalone::icons::ACTION_COPY
            };
        }
        return props;
    }

    const bool trackFocus =
        source.navigationFocus.get() == core::state::StructureNavigationFocus::TRACK;
    const auto context = macroInteractionContext(source);
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

FLASHMEM MacroViewFrameState buildMacroViewFrameState(const MacroViewModelSource& source) {
    MacroViewFrameState frame;

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto& config = source.pages.activeConfigs[i];
        const bool active = source.pages.isMacroSlotActive(i);
        const bool addSlot = !active && source.pages.isMacroAddSlot(i);
        const auto address = core::state::macro::MacroAutomationSlotAddress{
            .track = source.pages.currentActiveTrack(),
            .page = source.pages.currentActivePage(),
            .macro = i,
        };
        core::state::modulation::ProjectControlMacroSlotView controlSlot{};
        const bool controlSlotValid =
            core::state::modulation::readProjectControlMacroSlot(
                source.pages.control,
                address,
                controlSlot
            );
        const uint16_t overrideBit = static_cast<uint16_t>(1U << i);
        const bool manualOverride =
            (source.macroUi.automationManualOverrideMask.get() & overrideBit) != 0;
        const bool focused =
            source.navigationFocus.get() == core::state::StructureNavigationFocus::STEP &&
            source.macroUi.focusedMacroSlot.get() == i;
        const bool recording =
            source.macroUi.automationTake.phase ==
                core::state::macro::MacroAutomationTakePhase::RECORDING &&
            source.macroUi.automationTake.track == source.pages.currentActiveTrack() &&
            source.macroUi.automationTake.page == source.pages.currentActivePage() &&
            source.macroUi.automationTake.activeFor(i);
        const bool automationStored =
            controlSlotValid && controlSlot.automationStored;
        const bool modulationStored =
            controlSlotValid && controlSlot.modulationStored;
        const bool automationPlayback =
            automationStored && controlSlot.automationEnabled;
        const bool modulationPlayback =
            modulationStored && controlSlot.activeModulationCount > 0U;
        const bool modulationPaused =
            modulationPlayback && controlSlot.modulationCount == 1U &&
            controlSlot.compatibility.modulationDepth == 0.0f;
        const auto& projection = source.macroUi.runtimeProjections[i];
        const bool projectionValid =
            source.macroUi.runtimeProjectionValidFor(
                address.track,
                address.page,
                i
            );
        const float fallbackValue = source.macros.slots[i].value.get();
        frame.macros[i] = {
            .value = projectionValid ? projection.resolved : fallbackValue,
            .baseValue = projectionValid ? projection.base : fallbackValue,
            .modulationDelta = projectionValid ? projection.modulation : 0.0f,
            .modulationDepth = controlSlotValid
                ? controlSlot.compatibility.modulationDepth
                : 0.0f,
            .modulationSourceCount = static_cast<uint8_t>(
                controlSlotValid
                    ? std::min<uint16_t>(controlSlot.modulationCount, 7U)
                    : 0U
            ),
            .cc = config.cc,
            .automationStored = active && automationStored,
            .automationActive = active && automationPlayback && !manualOverride,
            .modulationStored = active && modulationStored,
            .modulationActive = active && modulationPlayback &&
                !modulationPaused,
            .modulationPaused = active && modulationPaused,
            .automationRecording = active && recording,
            .automationManualOverride = active && manualOverride,
            .clippedLow = projectionValid && projection.clippedLow,
            .clippedHigh = projectionValid && projection.clippedHigh,
            .active = active,
            .addSlot = addSlot,
            .focused = focused,
        };
    }

    return frame;
}

}  // namespace core::ui

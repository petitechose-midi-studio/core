#include "ui/view/MacroViewModelBuilder.hpp"

#include <cstdio>

#include <config/PlatformCompat.hpp>
#include "config/Timing.hpp"
#include "state/macro/MacroInteractionContextBuilder.hpp"
#include "state/macro/MacroInteractionPolicy.hpp"
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
            .slotPropertySelecting = source.macroUi.clutchActive.get(),
        }
    );
}

ContextActionStripVisualState macroVisual(
    core::state::macro::MacroInteractionVisibility visibility
) {
    switch (visibility) {
        case core::state::macro::MacroInteractionVisibility::ACTIVE:
            return ContextActionStripVisualState::ACTIVE;
        case core::state::macro::MacroInteractionVisibility::DIM:
            return ContextActionStripVisualState::DIM;
        case core::state::macro::MacroInteractionVisibility::HIDDEN:
        default:
            return ContextActionStripVisualState::HIDDEN;
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
    props.clutchActive = source.macroUi.clutchActive.get();
    props.focusingPage = focusingPage;
    props.focusingTrack = focusingTrack;
    props.selectingPage = selectingPage;
    props.selectingTrack = selectingTrack;
    props.previewPageAddSlot = previewPageAddSlot;
    props.previewTrackAddSlot = previewTrackAddSlot;
    props.automationRecording = source.macroUi.automationRecording.active;
    props.automationRecordingMacro = source.macroUi.automationRecording.address.macro;
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
    if (!source.macroUi.clutchActive.get()) {
        return {.visible = false};
    }

    const auto property = source.macroUi.activeProperty.get();
    StepPropertySelectionOverlayProps props{
        .visible = true,
        .customContent = true,
        .useValueText = true,
    };

    if (property == core::state::macro::MacroPerformanceProperty::AUTOMATION) {
        props.icon = standalone::icons::ACTION_REDO;
        props.label = "Automation";
        props.color = standalone::theme::color::MACRO_AUTOMATION;
        std::snprintf(props.valueText.data(), props.valueText.size(), "Restore");
        return props;
    }

    props.icon = standalone::icons::MIDI_CC;
    props.label = "CC";
    props.color = standalone::theme::color::MACRO_CC_COLOR;
    props.valueText[0] = '\0';
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

    props.slots[0].visualState = Visual::HIDDEN;
    props.slots[1].visualState = Visual::HIDDEN;
    props.slots[2].visualState = Visual::HIDDEN;

    return props;
}

FLASHMEM ContextActionStripProps buildMacroBottomActionStripProps(const MacroViewModelSource& source) {
    ContextActionStripProps props;
    props.visible = true;

    if (source.trackNavigation.selection.active.get() || source.macroUi.pageSelection.active.get()) {
        props.slots[0] = {
            .visualState = ContextActionStripVisualState::ACTIVE,
            .tone = ContextActionStripTone::DESTRUCTIVE,
            .showIcon = true,
            .icon = standalone::icons::ACTION_CLEAR
        };
        props.slots[1].visualState = ContextActionStripVisualState::HIDDEN;
        props.slots[2] = {
            .visualState = ContextActionStripVisualState::ACTIVE,
            .tone = ContextActionStripTone::CONSTRUCTIVE,
            .showIcon = true,
            .icon = standalone::icons::ACTION_COPY
        };
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
                                 : standalone::icons::ACTION_CLEAR,
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
        const auto* automation =
            core::state::macro::macroAutomationFindSlot(source.pages.automation, address);
        const uint16_t overrideBit = static_cast<uint16_t>(1U << i);
        const bool manualOverride =
            (source.macroUi.automationManualOverrideMask.get() & overrideBit) != 0;
        const bool focused =
            source.navigationFocus.get() == core::state::StructureNavigationFocus::STEP &&
            source.macroUi.focusedMacroSlot.get() == i;
        const bool recording =
            source.macroUi.automationRecording.active &&
            source.macroUi.automationRecording.address.track == source.pages.currentActiveTrack() &&
            source.macroUi.automationRecording.address.page == source.pages.currentActivePage() &&
            source.macroUi.automationRecording.address.macro == i;
        frame.macros[i] = {
            .value = source.macros.slots[i].value.get(),
            .cc = config.cc,
            .automationActive = active && automation != nullptr && automation->automation.active,
            .automationRecording = active && recording,
            .automationManualOverride = active && manualOverride,
            .active = active,
            .addSlot = addSlot,
            .focused = focused,
        };
    }

    return frame;
}

}  // namespace core::ui

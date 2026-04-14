#include "ui/view/MacroViewModelBuilder.hpp"

#include "config/Timing.hpp"
#include "state/shared/StructureSlotOps.hpp"
#include "ui/font/StandaloneIcons.hpp"

namespace core::ui {

namespace structure_slots = core::state::shared;

namespace {

uint8_t globalChannelForPage(const core::state::macro::MacroPagesState& pages) {
    return pages.activeConfigs[0].channel;
}

uint8_t previewGlobalChannelForView(const MacroViewModelSource& source) {
    if (source.macroUi.quickControlsSelecting.get()) {
        return source.macroUi.quickControlGlobalChannel.get();
    }
    return globalChannelForPage(source.pages);
}

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

}  // namespace

MacroHeaderBarProps buildMacroHeaderBarProps(const MacroViewModelSource& source) {
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

    props.pageOutputActivity.fill(0);
    if (source.statusBar.ccOutActive.get()) {
        props.pageOutputActivity[displayTrackData.activePage] = 127;
    }

    return props;
}

MacroBottomControlsProps buildMacroBottomControlsProps(const MacroViewModelSource& source) {
    return {
        .selectingQuickControls = source.macroUi.quickControlsSelecting.get(),
        .focusedQuickControl = source.macroUi.focusedQuickControl.get(),
        .globalChannel = previewGlobalChannelForView(source),
        .ccOffset = source.macroUi.ccOffset.get(),
    };
}

MacroPropertyStripProps buildMacroPropertyStripProps(const MacroViewModelSource& source) {
    return {
        .activeProperty = source.macroUi.activeProperty.get(),
        .clutchActive = source.macroUi.clutchActive.get(),
    };
}

ContextActionStripProps buildMacroLeftActionStripProps(const MacroViewModelSource& source) {
    using Visual = ContextActionStripVisualState;
    using Tone = ContextActionStripTone;

    const auto property = source.macroUi.activeProperty.get();
    const char* propertyIcon = standalone::icons::KNOB;
    if (property == core::state::macro::MacroPerformanceProperty::CC) {
        propertyIcon = standalone::icons::MIDI_CC;
    } else if (property == core::state::macro::MacroPerformanceProperty::CHANNEL) {
        propertyIcon = standalone::icons::MIDI_CHANNEL;
    }

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

    if (source.macroUi.quickControlsSelecting.get()) {
        props.slots[0] = {
            .visualState = Visual::ACTIVE,
            .tone = Tone::NEUTRAL,
            .showIcon = true,
            .icon = standalone::icons::ACTION_CANCEL
        };
        props.slots[1] = {
            .visualState = Visual::ACTIVE,
            .tone = Tone::NEUTRAL,
            .showIcon = true,
            .icon = standalone::icons::MIDI_CHANNEL
        };
        props.slots[2] = {
            .visualState = Visual::DIM,
            .tone = Tone::NEUTRAL,
            .showIcon = true,
            .icon = propertyIcon
        };
        return props;
    }

    props.slots[0].visualState = Visual::HIDDEN;
    props.slots[1] = {
        .visualState = source.macroUi.clutchActive.get() ? Visual::ACTIVE : Visual::DIM,
        .tone = Tone::NEUTRAL,
        .showIcon = true,
        .icon = standalone::icons::MIDI_CHANNEL
    };
    props.slots[2] = {
        .visualState = source.macroUi.clutchActive.get() ? Visual::ACTIVE : Visual::DIM,
        .tone = Tone::NEUTRAL,
        .showIcon = true,
        .icon = propertyIcon
    };

    return props;
}

ContextActionStripProps buildMacroBottomActionStripProps(const MacroViewModelSource& source) {
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
    const bool canPaste = trackFocus
        ? source.structureClipboard.hasMacroTrack()
        : source.structureClipboard.hasMacroPage();
    const auto& holdState = trackFocus ? source.trackNavigation.hold : source.macroUi.pageHold;
    const auto holdAction = holdState.action.get();
    const bool removeHoldActive = holdAction == core::state::StructureHoldAction::REMOVE;
    const bool pasteHoldActive = holdAction == core::state::StructureHoldAction::PASTE;

    props.slots[0] = {
        .visualState = removeHoldActive
            ? ContextActionStripVisualState::ARMED
            : ContextActionStripVisualState::ACTIVE,
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
            : ContextActionStripVisualState::ACTIVE),
        .tone = canPaste ? ContextActionStripTone::CONSTRUCTIVE : ContextActionStripTone::NEUTRAL,
        .showIcon = true,
        .icon = canPaste ? standalone::icons::ACTION_PASTE : standalone::icons::ACTION_COPY,
        .holdActive = pasteHoldActive,
        .holdStartedAtMs = holdState.startedAtMs.get(),
        .holdDurationMs = Config::Timing::OVERLAY_OPEN_LONG_PRESS_MS,
    };
    return props;
}

MacroViewFrameState buildMacroViewFrameState(const MacroViewModelSource& source) {
    MacroViewFrameState frame;

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto& config = source.pages.activeConfigs[i];
        frame.macros[i] = {
            .value = source.macros.slots[i].value.get(),
            .channel = config.channel,
            .cc = config.cc,
        };
    }

    return frame;
}

}  // namespace core::ui

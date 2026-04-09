#include "ui/view/MacroViewModelBuilder.hpp"

#include "ui/font/StandaloneIcons.hpp"

namespace core::ui {

namespace {

uint8_t globalChannelForPage(const core::state::macro::MacroPagesState& pages) {
    return pages.activeConfigs[0].channel;
}

}  // namespace

MacroHeaderBarProps buildMacroHeaderBarProps(const MacroViewModelSource& source) {
    MacroHeaderBarProps props;
    props.activeTrack = source.pages.activeTrack;
    props.activePage = source.pages.activePage;
    props.enabledMask = source.pages.enabledMask.get();
    props.trackEnabledMask = source.pages.trackEnabledMask.get();
    props.clutchActive = source.macroUi.clutchActive.get();
    props.selectingPage = source.macroUi.pageSelecting.get();
    props.previewPage = source.macroUi.pageSelecting.get()
        ? source.macroUi.selectedPage.get()
        : source.pages.activePage;

    props.pageOutputActivity.fill(0);
    if (source.statusBar.ccOutActive.get()) {
        props.pageOutputActivity[source.pages.activePage] = 127;
    }

    return props;
}

MacroBottomControlsProps buildMacroBottomControlsProps(const MacroViewModelSource& source) {
    return {
        .selectingQuickControls = source.macroUi.quickControlsSelecting.get(),
        .focusedQuickControl = source.macroUi.focusedQuickControl.get(),
        .globalChannel = globalChannelForPage(source.pages),
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

    if (source.macroUi.pageSelecting.get()) {
        props.slots[0] = {
            .visualState = Visual::ACTIVE,
            .tone = Tone::NEUTRAL,
            .showIcon = true,
            .icon = standalone::icons::ACTION_CANCEL
        };
        props.slots[1] = {
            .visualState = Visual::ACTIVE,
            .tone = Tone::NEUTRAL,
            .showLabel = true,
            .label = "PG"
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
    static_cast<void>(source);
    props.visible = true;
    for (auto& slot : props.slots) {
        slot.visualState = ContextActionStripVisualState::HIDDEN;
    }
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

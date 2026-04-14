#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/StructureSelectionState.hpp"

namespace core::state::macro {

enum class MacroPerformanceProperty : uint8_t {
    VALUE = 0,
    CC = 1,
    CHANNEL = 2,
};

enum class MacroQuickControlItem : uint8_t {
    GLOBAL_CHANNEL = 0,
    CC_OFFSET = 1,
};

struct MacroUiState {
    oc::state::Signal<MacroPerformanceProperty, 2> activeProperty{
        MacroPerformanceProperty::VALUE
    };
    oc::state::Signal<bool, 2> clutchActive{false};
    oc::state::Signal<bool, 2> quickControlsSelecting{false};
    oc::state::Signal<MacroQuickControlItem, 2> focusedQuickControl{
        MacroQuickControlItem::GLOBAL_CHANNEL
    };
    oc::state::Signal<uint8_t, 2> clutchPreviewTrackChannel{0};
    oc::state::Signal<uint8_t, 2> quickControlGlobalChannel{0};
    oc::state::Signal<int8_t, 2> ccOffset{0};
    oc::state::Signal<bool, 2> previewAddSlot{false};
    oc::state::Signal<uint8_t, 2> previewTrackIndex{0};
    oc::state::Signal<uint8_t, 2> previewPageIndex{0};
    core::state::StructureHoldState hold;
    core::state::StructureSelectionState structureSelection;

    void syncPreviewTrack(uint8_t trackIndex) {
        previewTrackIndex.set(trackIndex);
    }

    void syncPreviewPage(uint8_t pageIndex) {
        previewPageIndex.set(pageIndex);
    }

    void reset() {
        clutchActive.set(false);
        activeProperty.set(MacroPerformanceProperty::VALUE);
        quickControlsSelecting.set(false);
        focusedQuickControl.set(MacroQuickControlItem::GLOBAL_CHANNEL);
        clutchPreviewTrackChannel.set(0);
        quickControlGlobalChannel.set(0);
        ccOffset.set(0);
        previewAddSlot.set(false);
        previewTrackIndex.set(0);
        previewPageIndex.set(0);
        hold.clear();
        structureSelection.reset(core::state::StructureSelectionScope::PAGE);
    }
};

inline int performancePropertyIndex(MacroPerformanceProperty property) {
    switch (property) {
        case MacroPerformanceProperty::CC:
            return 1;
        case MacroPerformanceProperty::CHANNEL:
            return 2;
        case MacroPerformanceProperty::VALUE:
        default:
            return 0;
    }
}

inline MacroPerformanceProperty performancePropertyAtIndex(int index) {
    switch (index) {
        case 1:
            return MacroPerformanceProperty::CC;
        case 2:
            return MacroPerformanceProperty::CHANNEL;
        case 0:
        default:
            return MacroPerformanceProperty::VALUE;
    }
}

inline int quickControlIndex(MacroQuickControlItem item) {
    switch (item) {
        case MacroQuickControlItem::CC_OFFSET:
            return 1;
        case MacroQuickControlItem::GLOBAL_CHANNEL:
        default:
            return 0;
    }
}

inline MacroQuickControlItem quickControlAtIndex(int index) {
    switch (index) {
        case 1:
            return MacroQuickControlItem::CC_OFFSET;
        case 0:
        default:
            return MacroQuickControlItem::GLOBAL_CHANNEL;
    }
}

}  // namespace core::state::macro

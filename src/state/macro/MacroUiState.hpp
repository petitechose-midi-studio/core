#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/macro/MacroAutomationState.hpp"
#include "state/StructureSelectionState.hpp"

namespace core::state::macro {

/**
 * Session-only macro UI state.
 *
 * Runtime macro values and durable page data live in MacroState/MacroPagesState;
 * this struct tracks editor focus, slot property selection, recording state,
 * and structure selection UI.
 */
enum class MacroPerformanceProperty : uint8_t {
    VALUE = 0,
    CC = 1,
    AUTOMATION = 2,
};

enum class MacroAutomationRecordingStatus : uint8_t {
    IDLE = 0,
    RECORDING,
    REDUCED,
    TOO_SHORT,
    COMMIT_FAILED,
};

struct MacroUiState {
    struct AutomationRecordingState {
        bool active = false;
        MacroAutomationSlotAddress address{};
        uint32_t startedAtMs = 0;
        bool preserveDuration = false;
        uint16_t targetDurationTicks = MACRO_AUTOMATION_TICKS_PER_BEAT;
        MacroAutomationLane lane{};

        void reset() {
            active = false;
            address = {};
            startedAtMs = 0;
            preserveDuration = false;
            targetDurationTicks = MACRO_AUTOMATION_TICKS_PER_BEAT;
            lane = {};
        }
    };

    oc::state::Signal<MacroPerformanceProperty, 2> activeProperty{
        MacroPerformanceProperty::VALUE
    };
    oc::state::Signal<bool, 2> clutchActive{false};
    oc::state::Signal<uint32_t, 3> automationRecordingRevision{0};
    oc::state::Signal<MacroAutomationRecordingStatus, 3> automationRecordingStatus{
        MacroAutomationRecordingStatus::IDLE
    };
    oc::state::Signal<uint16_t, 4> automationManualOverrideMask{0};
    oc::state::Signal<uint8_t, 4> focusedMacroSlot{0};
    oc::state::Signal<bool, 2> previewAddPageSlot{false};
    oc::state::Signal<uint8_t, 2> previewPageIndex{0};
    core::state::StructureHoldState pageHold;
    core::state::StructureSelectionState pageSelection;
    AutomationRecordingState automationRecording;

    MacroUiState();
    ~MacroUiState();

    void syncPreviewPage(uint8_t pageIndex) {
        previewPageIndex.set(pageIndex);
    }

    void reset();
};

inline int performancePropertyIndex(MacroPerformanceProperty property) {
    switch (property) {
        case MacroPerformanceProperty::AUTOMATION:
            return 1;
        case MacroPerformanceProperty::CC:
        case MacroPerformanceProperty::VALUE:
        default:
            return 0;
    }
}

inline MacroPerformanceProperty performancePropertyAtIndex(int index) {
    switch (index) {
        case 1:
            return MacroPerformanceProperty::AUTOMATION;
        case 0:
        default:
            return MacroPerformanceProperty::CC;
    }
}

}  // namespace core::state::macro

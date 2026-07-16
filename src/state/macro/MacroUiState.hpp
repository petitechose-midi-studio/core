#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/macro/MacroAutomationState.hpp"
#include "state/macro/MacroRuntimeState.hpp"
#include "state/StructureSelectionState.hpp"
#include "state/contextual/GuardedActionState.hpp"
#include "state/contextual/OperationFeedbackState.hpp"

namespace core::state::macro {

constexpr uint8_t kMacroRuntimeProjectionDirtyAll = 0xFF;
constexpr uint8_t kMacroRuntimeProjectionDirtyConfig = 0xFE;

inline uint32_t nextMacroRuntimeProjectionRevision(
    uint32_t current,
    uint8_t dirtyIndex = kMacroRuntimeProjectionDirtyAll
) {
    uint32_t generation = ((current >> 8) + 1U) & 0x00FFFFFFU;
    if (generation == 0U) generation = 1U;
    return (generation << 8) | dirtyIndex;
}

inline bool macroRuntimeProjectionRevisionTargetsAll(uint32_t revision) {
    return static_cast<uint8_t>(revision & 0xFFU) ==
        kMacroRuntimeProjectionDirtyAll;
}

inline bool macroRuntimeProjectionRevisionTargetsConfig(uint32_t revision) {
    return static_cast<uint8_t>(revision & 0xFFU) ==
        kMacroRuntimeProjectionDirtyConfig;
}

inline int macroRuntimeProjectionRevisionDirtyIndex(uint32_t revision) {
    const uint8_t dirtyIndex = static_cast<uint8_t>(revision & 0xFFU);
    return dirtyIndex < MACRO_COUNT ? static_cast<int>(dirtyIndex) : -1;
}

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
    struct RuntimeValueProjection {
        float base = 0.0f;
        float modulation = 0.0f;
        float resolved = 0.0f;
        float modulationDepth = 0.0f;
        bool valid = false;
        bool modulationActive = false;
        bool clippedLow = false;
        bool clippedHigh = false;
    };

    struct AutomationRecordingState {
        bool active = false;
        MacroAutomationSlotAddress address{};
        uint32_t startedAtMs = 0;
        bool preserveDuration = false;
        uint16_t targetDurationTicks = MACRO_AUTOMATION_TICKS_PER_BEAT;
        bool restoreManualOnFailure = false;
        float previousManualValue = 0.0f;
        MacroAutomationLane lane{};

        void reset() {
            active = false;
            address = {};
            startedAtMs = 0;
            preserveDuration = false;
            targetDurationTicks = MACRO_AUTOMATION_TICKS_PER_BEAT;
            restoreManualOnFailure = false;
            previousManualValue = 0.0f;
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
    oc::state::Signal<uint32_t, 3> runtimeProjectionRevision{0};
    MacroManualOverrideState manualOverrides;
    std::array<RuntimeValueProjection, MACRO_COUNT> runtimeProjections{};
    oc::state::Signal<uint8_t, 4> focusedMacroSlot{0};
    oc::state::Signal<bool, 2> previewAddPageSlot{false};
    oc::state::Signal<uint8_t, 2> previewPageIndex{0};
    core::state::StructureHoldState pageHold;
    core::state::StructureSelectionState pageSelection;
    oc::state::Signal<core::state::contextual::GuardedActionState, 4>
        selectionDeleteGuard{};
    oc::state::Signal<core::state::contextual::OperationFeedbackState, 4>
        selectionDeleteFeedback{};
    AutomationRecordingState automationRecording;

    MacroUiState();
    ~MacroUiState();

    void syncPreviewPage(uint8_t pageIndex) {
        previewPageIndex.set(pageIndex);
    }

    /** Resets overlays/focus while retaining Project-scoped Manual entries. */
    void resetInteraction();
    /** Clears runtime Manual only at a Project load/create/reset boundary. */
    void resetProjectRuntime();
    /** Backward-compatible full reset; project lifecycle integration owns use. */
    void reset();
    void refreshManualOverrideMask(uint8_t track, uint8_t page);
    void setRuntimeProjection(uint8_t macro,
                              const MacroResolvedValue& value,
                              float modulationDepth);
    void clearRuntimeProjections();
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

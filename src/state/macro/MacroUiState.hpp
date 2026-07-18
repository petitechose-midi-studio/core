#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/macro/MacroAutomationTake.hpp"
#include "state/macro/MacroHistory.hpp"
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
    ARMED,
    RECORDING,
    REDUCED,
    TOO_SHORT,
    COMMIT_FAILED,
};

enum class MacroPerformanceOverlayMode : uint8_t {
    NONE = 0,
    EDIT,
    AUTOMATION_TAKE,
};

struct MacroUiState {
    static constexpr uint8_t INVALID_RUNTIME_PROJECTION_CONTEXT = 0xFFU;

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

    /**
     * Small stack token for one atomic runtime projection publication.
     * Values are staged in-place; no second Macro frame is retained.
     */
    struct RuntimeProjectionFrameTransaction {
        uint16_t previousValidMask = 0;
        uint16_t stagedValidMask = 0;
        uint16_t changedMask = 0;
        uint8_t previousContext = INVALID_RUNTIME_PROJECTION_CONTEXT;
        bool active = false;
    };

    oc::state::Signal<MacroPerformanceProperty, 2> activeProperty{
        MacroPerformanceProperty::VALUE
    };
    oc::state::Signal<bool, 2> clutchActive{false};
    oc::state::Signal<MacroPerformanceOverlayMode, 3> performanceOverlayMode{
        MacroPerformanceOverlayMode::NONE
    };
    oc::state::Signal<MacroAutomationTakeTiming, 3> automationTakeTiming{
        MacroAutomationTakeTiming::HOLD
    };
    oc::state::Signal<uint32_t, 3> automationRecordingRevision{0};
    oc::state::Signal<MacroAutomationRecordingStatus, 3> automationRecordingStatus{
        MacroAutomationRecordingStatus::IDLE
    };
    oc::state::Signal<uint16_t, 4> automationManualOverrideMask{0};
    oc::state::Signal<uint32_t, 3> runtimeProjectionRevision{0};
    MacroManualOverrideState manualOverrides;
    std::array<RuntimeValueProjection, MACRO_COUNT> runtimeProjections{};
    uint8_t runtimeProjectionContext = INVALID_RUNTIME_PROJECTION_CONTEXT;
    oc::state::Signal<uint8_t, 4> focusedMacroSlot{0};
    oc::state::Signal<bool, 2> previewAddPageSlot{false};
    oc::state::Signal<uint8_t, 2> previewPageIndex{0};
    core::state::StructureHoldState pageHold;
    core::state::StructureSelectionState pageSelection;
    oc::state::Signal<core::state::contextual::GuardedActionState, 4>
        selectionDeleteGuard{};
    oc::state::Signal<core::state::contextual::OperationFeedbackState, 4>
        selectionDeleteFeedback{};
    MacroAutomationTakeState automationTake;
    MacroHistoryChangePtr automationTakeHistory{};
    core::app::ExtmemUniquePtr<
        core::state::modulation::ProjectControlDomainState
    > automationTakeDomain{};

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
    void setRuntimeProjection(uint8_t track,
                              uint8_t page,
                              uint8_t macro,
                              const MacroResolvedValue& value,
                              float modulationDepth);
    [[nodiscard]] RuntimeProjectionFrameTransaction
        beginRuntimeProjectionFrame();
    void stageRuntimeProjection(
        RuntimeProjectionFrameTransaction& transaction,
        uint8_t macro,
        const MacroResolvedValue& value,
        float modulationDepth
    );
    void commitRuntimeProjectionFrame(
        RuntimeProjectionFrameTransaction& transaction,
        uint8_t track,
        uint8_t page
    );
    void cancelRuntimeProjectionFrame(
        RuntimeProjectionFrameTransaction& transaction
    );
    [[nodiscard]] bool runtimeProjectionValidFor(
        uint8_t track,
        uint8_t page,
        uint8_t macro
    ) const;
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

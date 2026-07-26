#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/macro/MacroAutomationTake.hpp"
#include "state/macro/MacroHistory.hpp"
#include "state/macro/MacroRuntimeState.hpp"
#include "state/modulation/ProjectRecordedShapeCaptureState.hpp"
#include "state/StructureNavigationState.hpp"

namespace core::state::macro {

constexpr uint8_t kMacroRuntimeProjectionDirtyAll = 0xFF;
constexpr uint8_t kMacroRuntimeProjectionDirtyConfig = 0xFE;
constexpr uint8_t kMacroAutomationRecordingDirtyAll = 0xFF;

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

inline uint32_t nextMacroAutomationRecordingRevision(
    uint32_t current,
    uint8_t dirtyIndex = kMacroAutomationRecordingDirtyAll
) {
    uint32_t generation = ((current >> 8) + 1U) & 0x00FFFFFFU;
    if (generation == 0U) generation = 1U;
    return (generation << 8) | dirtyIndex;
}

inline int macroAutomationRecordingRevisionDirtyIndex(uint32_t revision) {
    const uint8_t dirtyIndex = static_cast<uint8_t>(revision & 0xFFU);
    return dirtyIndex < MACRO_COUNT ? static_cast<int>(dirtyIndex) : -1;
}

/**
 * Session-only macro UI state.
 *
 * Runtime macro values and durable page data live in MacroState/MacroPagesState;
 * this struct tracks editor focus, slot property selection, recording state,
 * and direct structure navigation UI.
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

/** Temporary Macro root context selector; presentation is revision-driven. */
struct MacroContextSelectorState {
    bool visible = false;
    core::state::StructureNavigationFocus previewFocus =
        core::state::StructureNavigationFocus::PAGE;
    oc::state::Signal<uint32_t, 2> revision{0U};

    void show(core::state::StructureNavigationFocus focus) {
        visible = true;
        previewFocus = focus;
        bump();
    }
    void preview(core::state::StructureNavigationFocus focus) {
        previewFocus = focus;
        bump();
    }
    void hide() {
        if (!visible) return;
        visible = false;
        bump();
    }
    void reset() {
        visible = false;
        previewFocus = core::state::StructureNavigationFocus::PAGE;
        bump();
    }
    void bump() { revision.set(revision.get() + 1U); }
};

struct MacroUiState {
    static constexpr uint8_t INVALID_RUNTIME_PROJECTION_CONTEXT = 0xFFU;
    static constexpr uint32_t POST_TAKE_INPUT_GUARD_MS = 120U;

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
    /** Low-frequency authored Automation/Modulation mutation revision. */
    oc::state::Signal<uint32_t, 3> automationEditRevision{0};
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
    MacroContextSelectorState contextSelector;
    MacroAutomationTakeState automationTake;
    uint32_t postTakeInputGuardStartedAtMs = 0U;
    uint16_t postTakeInputGuardMask = 0U;
    MacroHistoryChangePtr automationTakeHistory{};
    core::app::ExtmemUniquePtr<
        core::state::modulation::ProjectControlDomainState
    > automationTakeDomain{};
    core::state::modulation::ProjectRecordedShapeCaptureState
        recordedShapeCapture{};
    /**
     * Observable mirror kept outside the compact capture transaction. Input
     * handlers publish it only after meaningful capture-state changes.
     */
    oc::state::Signal<uint32_t, 2> recordedShapeCaptureRevision{0U};

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
    void armPostTakeInputGuard(uint16_t macroMask, uint32_t nowMs);
    [[nodiscard]] bool blocksPostTakeInput(uint8_t macro, uint32_t nowMs);
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

#pragma once

#include <cstdint>

#include "state/MacroState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroHistory.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "handler/macro/MacroAutomationClipboardOps.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

enum class MacroSourceMode : uint8_t {
    OFF = 0,
    AUTOMATION,
    MODULATION,
    AUTO_MOD,
    MANUAL,
    SUSPENDED,
    PAUSED,
};

/**
 * Macro edit domain service boundary.
 *
 * Macro edit handlers use this service to read/apply active macro config and
 * switch pages through focused state refs and typed operations.
 */
class MacroEditDomainServices {
public:
    using SetConfigFn = bool (*)(void* context, uint8_t index, uint8_t channel, uint8_t cc);
    using SwitchToPageFn = void (*)(void* context, uint8_t pageIndex);
    using SwitchToTrackFn = void (*)(void* context, uint8_t trackIndex);
    using MarkProjectMutatedFn = void (*)(void* context);

    struct StateRefs {
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState* macroUi = nullptr;
        core::state::StructureClipboardState* clipboard = nullptr;
        core::state::MacroState* macros = nullptr;
        core::state::macro::MacroHistoryService* history = nullptr;
    };

    struct Operations {
        void* context = nullptr;
        SetConfigFn setConfig = nullptr;
        SwitchToPageFn switchToPage = nullptr;
        SwitchToTrackFn switchToTrack = nullptr;
        MarkProjectMutatedFn markProjectMutated = nullptr;
    };

    MacroEditDomainServices(StateRefs state, Operations operations);
    static MacroEditDomainServices fromCoreState(core::state::CoreState& state);

    const core::state::macro::MacroConfig& activeConfig(uint8_t index) const;
    bool isMacroSlotActive(uint8_t index) const;
    bool setConfig(uint8_t index, uint8_t channel, uint8_t cc) const;
    void switchToPage(uint8_t pageIndex) const;
    void switchToTrack(uint8_t trackIndex) const;
    core::state::macro::MacroAutomationSlotAddress automationAddress(uint8_t index) const;
    const core::state::macro::MacroAutomationSlotState* automationSlot(uint8_t index) const;
    bool automationClipboardAvailable() const;
    bool automationActiveFor(uint8_t index) const;
    bool automationStoredFor(uint8_t index) const;
    bool automationPlaybackActiveFor(uint8_t index) const;
    bool modulationStoredFor(uint8_t index) const;
    bool modulationPlaybackActiveFor(uint8_t index) const;
    float modulationDepth(uint8_t index) const;
    uint16_t modulationGlobalDepthQ15(uint8_t index) const;
    core::state::macro::MacroModulationOrigin modulationOrigin(uint8_t index) const;
    MacroSourceMode sourceModeFor(uint8_t index) const;
    bool manualOverrideActiveFor(uint8_t index) const;
    void setManualOverride(uint8_t index, bool active) const;
    bool setAutomationPlayback(uint8_t index, bool active) const;
    /** Aggregate bypass/restore for every assignment on the Macro. */
    bool setModulationPlayback(uint8_t index, bool active) const;
    const core::state::modulation::ModulationBindingState*
        focusedModulationBindingState(uint8_t index) const;
    bool setFocusedModulationPlayback(uint8_t index, bool active) const;
    bool removeFocusedModulation(uint8_t index) const;
    bool clearAutomation(uint8_t index) const;
    bool removeAutomation(uint8_t index) const;
    bool copyDestination(uint8_t index) const;
    macro::automation_clipboard_ops::MacroTypedPastePreflight
        preflightDestinationPaste(uint8_t index) const;
    bool pasteDestination(uint8_t index, bool overwriteConfirmed) const;
    bool copyAutomation(uint8_t index) const;
    bool pasteAutomation(uint8_t index) const;
    macro::automation_clipboard_ops::MacroTypedPastePreflight
        preflightAutomationPaste(uint8_t index) const;
    bool pasteAutomation(uint8_t index, bool overwriteConfirmed) const;
    core::state::macro::MacroAutomationConversionPlan preflightConversion(
        uint8_t index,
        core::state::macro::MacroAutomationConversionPolicy policy
    ) const;
    bool applyConversion(
        uint8_t index,
        const core::state::macro::MacroAutomationConversionPlan& plan,
        bool overwriteConfirmed
    ) const;
    bool resumeSources(uint8_t index) const;
    bool clearModulation(uint8_t index) const;
    bool removeSlot(uint8_t index) const;
    bool copySlot(uint8_t index) const;
    macro::automation_clipboard_ops::MacroTypedPastePreflight
        preflightSlotPaste(uint8_t index) const;
    bool pasteSlot(uint8_t index, bool overwriteConfirmed) const;
    bool copyModulation(uint8_t index) const;
    [[nodiscard]] bool hasModulationAssignmentClipboard() const;
    macro::automation_clipboard_ops::MacroTypedPastePreflight
        preflightModulationPaste(uint8_t index) const;
    bool pasteModulation(uint8_t index, bool overwriteConfirmed) const;
    core::state::modulation::ProjectModulationResult beginDefaultLfoAudition(
        uint8_t index
    ) const;
    core::state::modulation::ProjectModulationResult beginDefaultAdsrAudition(
        uint8_t index
    ) const;
    core::state::modulation::ProjectModulationResult
        beginExistingModulatorAudition(
            uint8_t index,
            core::state::modulation::ModulatorId sourceId
        ) const;
    bool setLfoAuditionShape(
        uint8_t index,
        core::state::modulation::ModulatorLfoShape shape
    ) const;
    bool setLfoAuditionPeriodTicks(uint8_t index, uint32_t periodTicks) const;
    bool setAdsrAuditionParameters(
        uint8_t index,
        const core::state::modulation::ModulatorAdsrParameters& parameters
    ) const;
    bool setModulatorAuditionDepthQ15(uint8_t index, int16_t depthQ15) const;
    bool cancelModulatorAudition(uint8_t index) const;
    bool applyModulatorAudition(uint8_t index) const;
    core::state::modulation::ModulationBindingId focusedModulationBinding(
        uint8_t index
    ) const;
    bool focusModulationBinding(
        uint8_t index,
        core::state::modulation::ModulationBindingId bindingId
    ) const;
    bool setModulationDepth(uint8_t index, float depth) const;
    bool setModulationGlobalDepthQ15(uint8_t index, uint16_t scaleQ15) const;
    void endDepthGesture() const;
    bool undo() const;
    bool redo() const;
    bool setAutomationDurationBeats(uint8_t index, float durationBeats) const;
    bool setAutomationWindowOffsetBeats(uint8_t index, float offsetBeats) const;

private:
    void publishModulationMutation_() const;
    core::state::macro::MacroPagesState* pages_ = nullptr;
    core::state::macro::MacroUiState* macro_ui_ = nullptr;
    core::state::StructureClipboardState* clipboard_ = nullptr;
    core::state::MacroState* macros_ = nullptr;
    core::state::macro::MacroHistoryService* history_ = nullptr;
    mutable core::state::modulation::ProjectControlMacroSlotView slot_view_cache_{};
    Operations operations_{};
};

}  // namespace core::handler

#pragma once

#include <cstdint>

#include "state/MacroState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroHistory.hpp"
#include "state/macro/MacroUiState.hpp"
#include "state/modulation/ProjectControlMacroOps.hpp"
#include "state/project/ProjectTrackState.hpp"
#include "handler/macro/MacroAutomationClipboardOps.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

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
    using SynchronizeSharedTrackStateFn = bool (*)(
        void* context,
        uint16_t enabledMask,
        uint8_t activeTrack
    );

    struct StateRefs {
        core::state::macro::MacroPagesState& pages;
        const core::state::project::ProjectTrackState& projectTracks;
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
        SynchronizeSharedTrackStateFn synchronizeSharedTrackState = nullptr;
    };

    MacroEditDomainServices(StateRefs state, Operations operations);
    static MacroEditDomainServices fromCoreState(core::state::CoreState& state);

    const core::state::macro::MacroConfig& activeConfig(uint8_t index) const;
    bool isMacroSlotActive(uint8_t index) const;
    bool setConfig(uint8_t index, uint8_t channel, uint8_t cc) const;
    void switchToPage(uint8_t pageIndex) const;
    void switchToTrack(uint8_t trackIndex) const;
    bool synchronizeSharedTrackState() const;
    core::state::macro::MacroAutomationSlotAddress automationAddress(uint8_t index) const;
    const core::state::modulation::ProjectControlMacroDestinationView*
        controlDestination(uint8_t index) const;
    bool automationActiveFor(uint8_t index) const;
    bool automationStoredFor(uint8_t index) const;
    bool automationPlaybackActiveFor(uint8_t index) const;
    bool modulationStoredFor(uint8_t index) const;
    bool modulationPlaybackActiveFor(uint8_t index) const;
    float modulationDepth(uint8_t index) const;
    uint16_t modulationGlobalDepthQ15(uint8_t index) const;
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
    bool copyDestination(uint8_t index) const;
    macro::automation_clipboard_ops::MacroTypedPastePreflight
        preflightDestinationPaste(uint8_t index) const;
    bool pasteDestination(uint8_t index, bool overwriteConfirmed) const;
    bool copyAutomation(uint8_t index) const;
    bool pasteAutomation(uint8_t index) const;
    macro::automation_clipboard_ops::MacroTypedPastePreflight
        preflightAutomationPaste(uint8_t index) const;
    bool pasteAutomation(uint8_t index, bool overwriteConfirmed) const;
    core::state::modulation::ProjectAutomationConversionPlan preflightConversion(
        uint8_t index,
        core::state::modulation::ProjectAutomationConversionPolicy policy
    ) const;
    bool applyConversion(
        uint8_t index,
        const core::state::modulation::ProjectAutomationConversionPlan& plan,
        bool overwriteConfirmed
    ) const;
    bool resumeSources(uint8_t index) const;
    bool clearModulation(uint8_t index) const;
    bool deleteSlot(uint8_t index) const;
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
    bool cancelModulatorAudition(uint8_t index) const;
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
    bool setAutomationDurationBeats(uint8_t index, float durationBeats) const;
    bool setAutomationWindowOffsetBeats(uint8_t index, float offsetBeats) const;
    /** Schedules Project persistence after an external history-backed writer. */
    void markProjectMutated() const;

private:
    void publishModulationMutation_() const;
    core::state::macro::MacroPagesState* pages_ = nullptr;
    const core::state::project::ProjectTrackState* project_tracks_ = nullptr;
    core::state::macro::MacroUiState* macro_ui_ = nullptr;
    core::state::StructureClipboardState* clipboard_ = nullptr;
    core::state::MacroState* macros_ = nullptr;
    core::state::macro::MacroHistoryService* history_ = nullptr;
    mutable core::state::modulation::ProjectControlMacroDestinationView
        destination_view_cache_{};
    Operations operations_{};
};

}  // namespace core::handler

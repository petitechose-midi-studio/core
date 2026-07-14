#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroHistory.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

/**
 * Macro performance domain service boundary.
 *
 * Input code receives focused macro/status refs and typed operations for
 * cross-domain effects such as project mutation and page/config workflows.
 * Manual values update persistable page intent; resolved values are transient
 * playback projections and never dirty the project.
 */
class MacroPerformanceDomainServices {
public:
    using MarkProjectMutatedFn = void (*)(void* context);
    using MarkMacroValueEditedFn = void (*)(void* context, uint8_t index);
    using SetConfigFn = bool (*)(void* context, uint8_t index, uint8_t channel, uint8_t cc);
    using SetTrackChannelFn = bool (*)(void* context, uint8_t channel);
    using SwitchToPageFn = void (*)(void* context, uint8_t pageIndex);

    struct StateRefs {
        core::state::MacroState& macros;
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
        oc::state::Signal<uint32_t>& configRevision;
        core::state::StatusBarState& statusBar;
        core::state::macro::MacroHistoryService* history = nullptr;
    };

    struct Operations {
        void* context = nullptr;
        MarkProjectMutatedFn markProjectMutated = nullptr;
        MarkMacroValueEditedFn markMacroValueEdited = nullptr;
        SetConfigFn setConfig = nullptr;
        SetTrackChannelFn setTrackChannel = nullptr;
        SwitchToPageFn switchToPage = nullptr;
    };

    MacroPerformanceDomainServices(StateRefs state, Operations operations);
    static MacroPerformanceDomainServices fromCoreState(core::state::CoreState& state);

    float runtimeValue(uint8_t index) const;
    /// Apply user/MIDI input to both runtime feedback and persisted base intent.
    void setManualValue(uint8_t index, float value) const;
    /// Apply computed playback feedback without changing persisted base intent.
    void setResolvedValue(uint8_t index, float value) const;
    bool beginAutomationRecording(uint8_t index, uint32_t nowMs) const;
    bool recordAutomationPoint(uint8_t index, uint32_t nowMs, float value) const;
    bool commitAutomationRecording(uint32_t nowMs) const;
    bool cancelAutomationRecording() const;
    bool automationRecordingActiveFor(uint8_t index) const;
    /// True when Automation or Modulation is stored and enabled for playback.
    bool computedSourcePlaybackActiveFor(uint8_t index) const;
    bool automationActiveFor(uint8_t index) const;
    bool manualOverrideActiveFor(uint8_t index) const;
    bool manualOverrideValueFor(uint8_t index, float& outValue) const;
    /// Takes final-value ownership without changing the persisted static base.
    bool takeManualControl(uint8_t index, float value) const;
    /// Releases Manual and restores the exact enabled computed-source set.
    bool resumeComputedSources(uint8_t index) const;
    bool isMacroSlotActive(uint8_t index) const;
    bool isMacroAddSlot(uint8_t index) const;
    bool activateMacroSlot(uint8_t index) const;
    const core::state::macro::MacroConfig& activeConfig(uint8_t index) const;
    bool setConfig(uint8_t index, uint8_t channel, uint8_t cc) const;
    bool setTrackConfigs(
        const std::array<core::state::macro::MacroConfig, core::state::macro::MACRO_COUNT>& configs
    ) const;
    uint8_t activeTrackChannel() const;
    bool setTrackChannel(uint8_t channel) const;
    bool isActivePageEnabled() const;
    void switchToPage(uint8_t pageIndex) const;
    void pulseCcIn() const;
    void pulseCcOut() const;
    void pulseNoteIn() const;

private:
    core::state::macro::MacroAutomationSlotAddress activeAddress_(uint8_t index) const;
    void refreshManualProjection_() const;
    void restoreManualAfterFailedRecording_(
        const core::state::macro::MacroUiState::AutomationRecordingState& recording
    ) const;

    core::state::MacroState* macros_ = nullptr;
    core::state::macro::MacroPagesState* pages_ = nullptr;
    core::state::macro::MacroUiState* macro_ui_ = nullptr;
    oc::state::Signal<uint32_t>* config_revision_ = nullptr;
    core::state::StatusBarState* status_bar_ = nullptr;
    core::state::macro::MacroHistoryService* history_ = nullptr;
    Operations operations_{};
};

}  // namespace core::handler

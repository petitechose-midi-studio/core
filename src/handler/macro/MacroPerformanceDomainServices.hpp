#pragma once

#include <array>
#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroPagesState.hpp"

namespace core::state {
struct CoreState;
namespace macro {
struct MacroUiState;
}
}

namespace core::handler {

/**
 * Macro performance domain service boundary.
 *
 * Input code receives focused macro/status refs and typed operations for
 * cross-domain effects such as project mutation and page/config workflows.
 */
class MacroPerformanceDomainServices {
public:
    using MarkProjectMutatedFn = void (*)(void* context);
    using SetConfigFn = bool (*)(void* context, uint8_t index, uint8_t channel, uint8_t cc);
    using SetTrackChannelFn = bool (*)(void* context, uint8_t channel);
    using SwitchToPageFn = void (*)(void* context, uint8_t pageIndex);

    struct StateRefs {
        core::state::MacroState& macros;
        core::state::macro::MacroPagesState& pages;
        core::state::macro::MacroUiState& macroUi;
        oc::state::Signal<uint32_t>& configRevision;
        core::state::StatusBarState& statusBar;
    };

    struct Operations {
        void* context = nullptr;
        MarkProjectMutatedFn markProjectMutated = nullptr;
        SetConfigFn setConfig = nullptr;
        SetTrackChannelFn setTrackChannel = nullptr;
        SwitchToPageFn switchToPage = nullptr;
    };

    MacroPerformanceDomainServices(StateRefs state, Operations operations);
    static MacroPerformanceDomainServices fromCoreState(core::state::CoreState& state);

    float runtimeValue(uint8_t index) const;
    void setRuntimeValue(uint8_t index, float value) const;
    bool beginAutomationRecording(uint8_t index, uint32_t nowMs) const;
    bool recordAutomationPoint(uint8_t index, uint32_t nowMs, float value) const;
    bool commitAutomationRecording(uint32_t nowMs) const;
    bool cancelAutomationRecording() const;
    bool automationRecordingActiveFor(uint8_t index) const;
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
    core::state::MacroState* macros_ = nullptr;
    core::state::macro::MacroPagesState* pages_ = nullptr;
    core::state::macro::MacroUiState* macro_ui_ = nullptr;
    oc::state::Signal<uint32_t>* config_revision_ = nullptr;
    core::state::StatusBarState* status_bar_ = nullptr;
    Operations operations_{};
};

}  // namespace core::handler

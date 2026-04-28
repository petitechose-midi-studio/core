#pragma once

#include <array>
#include <cstdint>

#include "state/macro/MacroWorkflow.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

/**
 * CoreState bridge for macro performance workflows.
 *
 * This service wraps MacroWorkflow and status pulses so input code can mutate
 * runtime values/configuration without owning CoreState internals.
 */
class MacroPerformanceDomainServices {
public:
    explicit MacroPerformanceDomainServices(core::state::CoreState& state);
    static MacroPerformanceDomainServices fromCoreState(core::state::CoreState& state);

    float runtimeValue(uint8_t index) const;
    void setRuntimeValue(uint8_t index, float value) const;
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
    core::state::CoreState* state_ = nullptr;
};

}  // namespace core::handler

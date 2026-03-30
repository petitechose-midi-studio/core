#pragma once

#include <cstdint>

#include "state/macro/MacroWorkflow.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class MacroDomainServices {
public:
    using StateRefs = core::state::macro::MacroWorkflow::StateRefs;
    using Hooks = core::state::macro::MacroWorkflow::Hooks;

    MacroDomainServices(StateRefs state, Hooks hooks);
    static MacroDomainServices fromCoreState(core::state::CoreState& state);

    float runtimeValue(uint8_t index) const;
    void setRuntimeValue(uint8_t index, float value) const;
    const core::state::macro::MacroConfig& activeConfig(uint8_t index) const;
    bool setConfig(uint8_t index, uint8_t channel, uint8_t cc) const;
    void switchToPage(uint8_t pageIndex) const;

    void pulseCcIn() const;
    void pulseCcOut() const;
    void pulseNoteIn() const;

private:
    core::state::MacroState* macros_ = nullptr;
    core::state::macro::MacroPagesState* pages_ = nullptr;
    oc::state::Signal<uint32_t>* config_revision_ = nullptr;
    core::state::StatusBarState* status_bar_ = nullptr;
    Hooks hooks_{};
};

}  // namespace core::handler

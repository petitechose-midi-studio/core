#pragma once

#include <cstdint>

#include "state/macro/MacroWorkflow.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class MacroEditDomainServices {
public:
    explicit MacroEditDomainServices(core::state::CoreState& state);
    static MacroEditDomainServices fromCoreState(core::state::CoreState& state);

    const core::state::macro::MacroConfig& activeConfig(uint8_t index) const;
    bool setConfig(uint8_t index, uint8_t channel, uint8_t cc) const;
    void switchToPage(uint8_t pageIndex) const;

private:
    core::state::CoreState* state_ = nullptr;
};

}  // namespace core::handler

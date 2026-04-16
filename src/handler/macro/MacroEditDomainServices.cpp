#include "handler/macro/MacroEditDomainServices.hpp"

#include "state/CoreState.hpp"

namespace core::handler {

MacroEditDomainServices::MacroEditDomainServices(core::state::CoreState& state)
    : state_(&state) {}

MacroEditDomainServices MacroEditDomainServices::fromCoreState(core::state::CoreState& state) {
    return MacroEditDomainServices{state};
}

const core::state::macro::MacroConfig& MacroEditDomainServices::activeConfig(uint8_t index) const {
    return core::state::macro::MacroWorkflow::activeConfig(state_->pages, index);
}

bool MacroEditDomainServices::setConfig(uint8_t index, uint8_t channel, uint8_t cc) const {
    return core::state::macro::MacroWorkflow::setConfig(*state_, index, channel, cc);
}

void MacroEditDomainServices::switchToPage(uint8_t pageIndex) const {
    core::state::macro::MacroWorkflow::switchToPage(*state_, pageIndex);
}

}  // namespace core::handler

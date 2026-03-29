#include "handler/macro/MacroDomainServices.hpp"

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {

MacroDomainServices::MacroDomainServices(core::state::CoreState& state) : state_(&state) {}

float MacroDomainServices::runtimeValue(uint8_t index) const {
    return core::state::macro::MacroWorkflow::runtimeValue(*state_, index);
}

void MacroDomainServices::setRuntimeValue(uint8_t index, float value) const {
    core::state::macro::MacroWorkflow::setRuntimeValue(*state_, index, value);
}

const core::state::macro::MacroConfig& MacroDomainServices::activeConfig(uint8_t index) const {
    return core::state::macro::MacroWorkflow::activeConfig(*state_, index);
}

bool MacroDomainServices::setConfig(uint8_t index, uint8_t channel, uint8_t cc) const {
    return core::state::macro::MacroWorkflow::setConfig(*state_, index, channel, cc);
}

void MacroDomainServices::switchToPage(uint8_t pageIndex) const {
    core::state::macro::MacroWorkflow::switchToPage(*state_, pageIndex);
}

void MacroDomainServices::pulseCcIn() const {
    state_->statusBar.pulseCcIn();
}

void MacroDomainServices::pulseCcOut() const {
    state_->statusBar.pulseCcOut();
}

void MacroDomainServices::pulseNoteIn() const {
    state_->statusBar.pulseNoteIn();
}

}  // namespace core::handler

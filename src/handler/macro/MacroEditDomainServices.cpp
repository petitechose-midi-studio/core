#include "handler/macro/MacroEditDomainServices.hpp"

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {

namespace {

bool setConfigFromCoreState(void* context, uint8_t index, uint8_t channel, uint8_t cc) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr &&
           core::state::macro::MacroWorkflow::setConfig(*state, index, channel, cc);
}

void switchToPageFromCoreState(void* context, uint8_t pageIndex) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    core::state::macro::MacroWorkflow::switchToPage(*state, pageIndex);
}

}  // namespace

MacroEditDomainServices::MacroEditDomainServices(StateRefs state, Operations operations)
    : pages_(&state.pages)
    , operations_(operations) {}

MacroEditDomainServices MacroEditDomainServices::fromCoreState(core::state::CoreState& state) {
    return MacroEditDomainServices{
        StateRefs{state.pages},
        Operations{
            &state,
            setConfigFromCoreState,
            switchToPageFromCoreState,
        },
    };
}

const core::state::macro::MacroConfig& MacroEditDomainServices::activeConfig(uint8_t index) const {
    return core::state::macro::MacroWorkflow::activeConfig(*pages_, index);
}

bool MacroEditDomainServices::setConfig(uint8_t index, uint8_t channel, uint8_t cc) const {
    return operations_.setConfig != nullptr &&
           operations_.setConfig(operations_.context, index, channel, cc);
}

void MacroEditDomainServices::switchToPage(uint8_t pageIndex) const {
    if (operations_.switchToPage != nullptr) {
        operations_.switchToPage(operations_.context, pageIndex);
    }
}

}  // namespace core::handler

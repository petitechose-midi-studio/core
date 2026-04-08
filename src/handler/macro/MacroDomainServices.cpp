#include "handler/macro/MacroDomainServices.hpp"

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {

MacroDomainServices::MacroDomainServices(StateRefs state, Hooks hooks)
    : macros_(&state.macros)
    , pages_(&state.pages)
    , config_revision_(&state.configRevision)
    , status_bar_(&state.statusBar)
    , hooks_(hooks) {}

MacroDomainServices MacroDomainServices::fromCoreState(core::state::CoreState& state) {
    return MacroDomainServices{
        StateRefs{
            state.macros,
            state.pages,
            state.configRevision,
            state.statusBar,
        },
        Hooks{&state},
    };
}

float MacroDomainServices::runtimeValue(uint8_t index) const {
    return core::state::macro::MacroWorkflow::runtimeValue(*macros_, index);
}

void MacroDomainServices::setRuntimeValue(uint8_t index, float value) const {
    core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, value);
}

const core::state::macro::MacroConfig& MacroDomainServices::activeConfig(uint8_t index) const {
    return core::state::macro::MacroWorkflow::activeConfig(*pages_, index);
}

bool MacroDomainServices::setConfig(uint8_t index, uint8_t channel, uint8_t cc) const {
    return core::state::macro::MacroWorkflow::setConfig(
        core::state::macro::MacroWorkflow::StateRefs{
            *macros_,
            *pages_,
            *config_revision_,
            *status_bar_,
        },
        hooks_,
        index,
        channel,
        cc
    );
}

void MacroDomainServices::switchToPage(uint8_t pageIndex) const {
    core::state::macro::MacroWorkflow::switchToPage(
        core::state::macro::MacroWorkflow::StateRefs{
            *macros_,
            *pages_,
            *config_revision_,
            *status_bar_,
        },
        hooks_,
        pageIndex
    );
}

bool MacroDomainServices::isActivePageEnabled() const {
    return pages_->isPageEnabled(pages_->activePage);
}

void MacroDomainServices::togglePageEnabled(uint8_t pageIndex) const {
    pages_->togglePageEnabled(pageIndex);
}

void MacroDomainServices::setPageEnabledMask(uint8_t mask) const {
    pages_->enabledMask.set(mask);
}

uint8_t MacroDomainServices::pageEnabledMask() const {
    return pages_->enabledMask.get();
}

void MacroDomainServices::pulseCcIn() const {
    status_bar_->pulseCcIn();
}

void MacroDomainServices::pulseCcOut() const {
    status_bar_->pulseCcOut();
}

void MacroDomainServices::pulseNoteIn() const {
    status_bar_->pulseNoteIn();
}

}  // namespace core::handler

#include "handler/macro/MacroPerformanceDomainServices.hpp"

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::handler {

namespace {

bool setTrackConfigsImpl(
    MacroPerformanceDomainServices::StateRefs state,
    MacroPerformanceDomainServices::Operations operations,
    const std::array<core::state::macro::MacroConfig, core::state::macro::MACRO_COUNT>& configs
) {
    const uint8_t targetChannel = configs[0].channel;
    for (uint8_t i = 1; i < core::state::macro::MACRO_COUNT; ++i) {
        if (configs[i].channel != targetChannel) {
            return false;
        }
    }

    const bool channelChanged = state.pages.activeTrackChannel() != targetChannel;
    bool anyCcChanged = false;

    auto& page = state.pages.activePageData();
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        if (page.cc[i] == configs[i].cc) continue;
        page.cc[i] = configs[i].cc;
        anyCcChanged = true;
    }

    if (!channelChanged && !anyCcChanged) {
        return false;
    }

    if (channelChanged) {
        state.pages.setActiveTrackChannel(targetChannel);
    } else {
        state.pages.updateActiveConfigs();
    }

    state.configRevision.set(
        core::state::macro::nextMacroConfigRevision(state.configRevision.get())
    );
    if (operations.markProjectMutated != nullptr) {
        operations.markProjectMutated(operations.context);
    }
    return true;
}

void markProjectMutatedFromCoreState(void* context) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    state->markProjectMutated();
}

bool setConfigFromCoreState(void* context, uint8_t index, uint8_t channel, uint8_t cc) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr &&
           core::state::macro::MacroWorkflow::setConfig(*state, index, channel, cc);
}

bool setTrackChannelFromCoreState(void* context, uint8_t channel) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr && core::state::macro::MacroWorkflow::setTrackChannel(*state, channel);
}

void switchToPageFromCoreState(void* context, uint8_t pageIndex) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) return;
    core::state::macro::MacroWorkflow::switchToPage(*state, pageIndex);
}

}  // namespace

MacroPerformanceDomainServices::MacroPerformanceDomainServices(
    StateRefs state,
    Operations operations
)
    : macros_(&state.macros)
    , pages_(&state.pages)
    , config_revision_(&state.configRevision)
    , status_bar_(&state.statusBar)
    , operations_(operations) {}

MacroPerformanceDomainServices MacroPerformanceDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return MacroPerformanceDomainServices{
        StateRefs{
            state.macros,
            state.pages,
            state.configRevision,
            state.statusBar,
        },
        Operations{
            &state,
            markProjectMutatedFromCoreState,
            setConfigFromCoreState,
            setTrackChannelFromCoreState,
            switchToPageFromCoreState,
        },
    };
}

float MacroPerformanceDomainServices::runtimeValue(uint8_t index) const {
    return core::state::macro::MacroWorkflow::runtimeValue(*macros_, index);
}

void MacroPerformanceDomainServices::setRuntimeValue(uint8_t index, float value) const {
    core::state::macro::MacroWorkflow::setRuntimeValue(*macros_, index, value);
}

const core::state::macro::MacroConfig& MacroPerformanceDomainServices::activeConfig(
    uint8_t index
) const {
    return core::state::macro::MacroWorkflow::activeConfig(*pages_, index);
}

bool MacroPerformanceDomainServices::setConfig(uint8_t index,
                                               uint8_t channel,
                                               uint8_t cc) const {
    return operations_.setConfig != nullptr &&
           operations_.setConfig(operations_.context, index, channel, cc);
}

bool MacroPerformanceDomainServices::setTrackConfigs(
    const std::array<core::state::macro::MacroConfig, core::state::macro::MACRO_COUNT>& configs
) const {
    return setTrackConfigsImpl(
        StateRefs{*macros_, *pages_, *config_revision_, *status_bar_},
        operations_,
        configs
    );
}

uint8_t MacroPerformanceDomainServices::activeTrackChannel() const {
    return pages_->activeTrackChannel();
}

bool MacroPerformanceDomainServices::setTrackChannel(uint8_t channel) const {
    return operations_.setTrackChannel != nullptr &&
           operations_.setTrackChannel(operations_.context, channel);
}

bool MacroPerformanceDomainServices::isActivePageEnabled() const {
    return pages_->isPageEnabled(pages_->currentActivePage());
}

void MacroPerformanceDomainServices::switchToPage(uint8_t pageIndex) const {
    if (operations_.switchToPage != nullptr) {
        operations_.switchToPage(operations_.context, pageIndex);
    }
}

void MacroPerformanceDomainServices::pulseCcIn() const {
    status_bar_->pulseCcIn();
}

void MacroPerformanceDomainServices::pulseCcOut() const {
    status_bar_->pulseCcOut();
}

void MacroPerformanceDomainServices::pulseNoteIn() const {
    status_bar_->pulseNoteIn();
}

}  // namespace core::handler

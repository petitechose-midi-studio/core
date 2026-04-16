#include "handler/macro/MacroPerformanceDomainServices.hpp"

#include "state/CoreState.hpp"

namespace core::handler {

namespace {

bool setTrackConfigsImpl(
    core::state::CoreState& state,
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
    state.requestMacroWorkspacePersist();
    return true;
}

}  // namespace

MacroPerformanceDomainServices::MacroPerformanceDomainServices(core::state::CoreState& state)
    : state_(&state) {}

MacroPerformanceDomainServices MacroPerformanceDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return MacroPerformanceDomainServices{state};
}

float MacroPerformanceDomainServices::runtimeValue(uint8_t index) const {
    return core::state::macro::MacroWorkflow::runtimeValue(state_->macros, index);
}

void MacroPerformanceDomainServices::setRuntimeValue(uint8_t index, float value) const {
    state_->noteMacroInteraction();
    core::state::macro::MacroWorkflow::setRuntimeValue(state_->macros, index, value);
}

const core::state::macro::MacroConfig& MacroPerformanceDomainServices::activeConfig(
    uint8_t index
) const {
    return core::state::macro::MacroWorkflow::activeConfig(state_->pages, index);
}

bool MacroPerformanceDomainServices::setConfig(uint8_t index,
                                               uint8_t channel,
                                               uint8_t cc) const {
    return core::state::macro::MacroWorkflow::setConfig(*state_, index, channel, cc);
}

bool MacroPerformanceDomainServices::setTrackConfigs(
    const std::array<core::state::macro::MacroConfig, core::state::macro::MACRO_COUNT>& configs
) const {
    return setTrackConfigsImpl(*state_, configs);
}

uint8_t MacroPerformanceDomainServices::activeTrackChannel() const {
    return state_->pages.activeTrackChannel();
}

bool MacroPerformanceDomainServices::setTrackChannel(uint8_t channel) const {
    return core::state::macro::MacroWorkflow::setTrackChannel(*state_, channel);
}

bool MacroPerformanceDomainServices::isActivePageEnabled() const {
    return state_->pages.isPageEnabled(state_->pages.currentActivePage());
}

void MacroPerformanceDomainServices::switchToPage(uint8_t pageIndex) const {
    core::state::macro::MacroWorkflow::switchToPage(*state_, pageIndex);
}

void MacroPerformanceDomainServices::pulseCcIn() const {
    state_->statusBar.pulseCcIn();
}

void MacroPerformanceDomainServices::pulseCcOut() const {
    state_->statusBar.pulseCcOut();
}

void MacroPerformanceDomainServices::pulseNoteIn() const {
    state_->statusBar.pulseNoteIn();
}

}  // namespace core::handler

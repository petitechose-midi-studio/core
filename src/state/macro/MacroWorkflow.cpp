#include "state/macro/MacroWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include "state/CoreState.hpp"
#include "state/macro/MacroAutomationDomain.hpp"

namespace core::state::macro {

namespace {

FLASHMEM bool configsMatch(
    const std::array<MacroConfig, MACRO_COUNT>& lhs,
    const std::array<MacroConfig, MACRO_COUNT>& rhs
) {
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        if (lhs[i].cc != rhs[i].cc || lhs[i].channel != rhs[i].channel) {
            return false;
        }
    }
    return true;
}

}  // namespace

FLASHMEM void MacroWorkflow::syncRuntimeFromActivePage(core::state::MacroState& macros,
                                                       const MacroPagesState& pages) {
    const auto& pageData = pages.activePageData();
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        char label[16];
        size_t pos = oc::type::text::appendString(label, sizeof(label), 0, "Macro ");
        pos = oc::type::text::appendUnsigned(label, sizeof(label), pos, i + 1);
        oc::type::text::terminate(label, sizeof(label), pos);
        macros.slots[i].label.set(label);
        setRuntimeValue(macros, i, pageData.values[i]);
    }
}

FLASHMEM void MacroWorkflow::switchToPage(CoreState& state, uint8_t pageIndex) {
    if (pageIndex >= PAGE_COUNT) return;

    const auto previousConfigs = state.pages.activeConfigs;
    state.flushProjectMutationCoalescing();
    state.pages.setActivePage(pageIndex);
    state.markProjectMutated();
    if (!configsMatch(previousConfigs, state.pages.activeConfigs)) {
        state.configRevision.set(nextMacroConfigRevision(state.configRevision.get()));
    }
    state.statusBar.pageName.set(state.pages.activePageData().name);
    syncRuntimeFromActivePage(state.macros, state.pages);
}

FLASHMEM void MacroWorkflow::switchToTrack(CoreState& state, uint8_t trackIndex) {
    if (trackIndex >= TRACK_COUNT) return;

    const auto previousConfigs = state.pages.activeConfigs;
    state.flushProjectMutationCoalescing();
    state.setSharedTrackState(state.currentSharedTrackEnabledMask(), trackIndex);
    state.markProjectMutated();
    if (!configsMatch(previousConfigs, state.pages.activeConfigs)) {
        state.configRevision.set(nextMacroConfigRevision(state.configRevision.get()));
    }
    state.statusBar.pageName.set(state.pages.activePageData().name);
    syncRuntimeFromActivePage(state.macros, state.pages);
}

FLASHMEM bool MacroWorkflow::setConfig(CoreState& state, uint8_t index, uint8_t channel, uint8_t cc) {
    if (index >= MACRO_COUNT) return false;
    if (channel > 15 || cc > 127) return false;

    auto& page = state.pages.activePageData();
    const bool channelChanged = state.pages.activeTrackChannel() != channel;
    const bool ccChanged = page.cc[index] != cc;
    if (!channelChanged && !ccChanged) {
        return false;
    }

    if (channelChanged) {
        state.pages.setActiveTrackChannel(channel);
    }
    page.cc[index] = cc;
    state.pages.updateActiveConfigs();
    state.configRevision.set(nextMacroConfigRevision(
        state.configRevision.get(),
        channelChanged ? kMacroConfigDirtyAll : index
    ));
    state.markProjectMutated();
    return true;
}

FLASHMEM bool MacroWorkflow::setTrackChannel(CoreState& state, uint8_t channel) {
    if (channel > 15) return false;
    if (state.pages.activeTrackChannel() == channel) return false;

    state.pages.setActiveTrackChannel(channel);
    state.configRevision.set(nextMacroConfigRevision(state.configRevision.get()));
    state.markProjectMutated();
    return true;
}

FLASHMEM MacroSlotActivationPlan MacroWorkflow::planMacroSlotActivation(
    const MacroPagesState& pages,
    const MacroAutomationSlotAddress& address
) {
    MacroSlotActivationPlan plan{};
    plan.address = address;
    if (!macroAutomationAddressValid(address)) return plan;

    const auto& page = pages.pageData(address.track, address.page);
    if (page.isMacroActive(address.macro) ||
        page.nextAddMacroIndex() != address.macro) {
        return plan;
    }

    uint8_t sourceIndex = 0;
    for (uint8_t i = 0; i < address.macro; ++i) {
        if (page.isMacroActive(i)) sourceIndex = i;
    }
    const uint16_t nextCc = static_cast<uint16_t>(page.cc[sourceIndex]) + 1U;
    plan.cc = static_cast<uint8_t>(nextCc > 127U ? 127U : nextCc);
    plan.baseValue = 0.5f;
    plan.valid = true;
    return plan;
}

FLASHMEM bool MacroWorkflow::applyMacroSlotActivation(
    MacroPagesState& pages,
    const MacroSlotActivationPlan& plan
) {
    if (!plan.valid || !macroAutomationAddressValid(plan.address)) return false;
    const auto current = planMacroSlotActivation(pages, plan.address);
    if (!current.valid || current.cc != plan.cc ||
        current.baseValue != plan.baseValue) {
        return false;
    }

    auto& page = pages.pageData(plan.address.track, plan.address.page);
    page.cc[plan.address.macro] = plan.cc;
    page.values[plan.address.macro] = plan.baseValue;
    page.setMacroActive(plan.address.macro, true);
    if (pages.currentActiveTrack() == plan.address.track &&
        pages.currentActivePage() == plan.address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

FLASHMEM bool MacroWorkflow::activateMacroSlot(core::state::MacroState& macros,
                                               MacroPagesState& pages,
                                               uint8_t index) {
    const MacroAutomationSlotAddress address{
        pages.currentActiveTrack(),
        pages.currentActivePage(),
        index,
    };
    const auto plan = planMacroSlotActivation(pages, address);
    if (!applyMacroSlotActivation(pages, plan)) return false;
    setRuntimeValue(macros, index, plan.baseValue);
    return true;
}

void MacroWorkflow::setRuntimeValue(core::state::MacroState& macros, uint8_t index, float value) {
    if (index >= MACRO_COUNT) return;
    macros.slots[index].value.set(macroAutomationClamp01(value));
}

float MacroWorkflow::runtimeValue(const core::state::MacroState& macros, uint8_t index) {
    if (index >= MACRO_COUNT) return 0.0f;
    return macros.slots[index].value.get();
}

const MacroConfig& MacroWorkflow::activeConfig(const MacroPagesState& pages, uint8_t index) {
    static const MacroConfig defaultConfig{};
    if (index >= MACRO_COUNT) return defaultConfig;
    return pages.activeConfigs[index];
}

}  // namespace core::state::macro

#include "state/macro/MacroWorkflow.hpp"

#include <algorithm>
#include <config/PlatformCompat.hpp>
#include <oc/type/TextFormat.hpp>

#include "state/CoreState.hpp"

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
        macros.slots[i].value.set(std::clamp(pageData.values[i], 0.0f, 1.0f));
    }
}

FLASHMEM void MacroWorkflow::syncActivePageValuesFromRuntime(
    MacroPagesState& pages,
    const core::state::MacroState& macros
) {
    auto& page = pages.activePageData();
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        page.values[i] = std::clamp(macros.slots[i].value.get(), 0.0f, 1.0f);
    }
}

FLASHMEM void MacroWorkflow::switchToPage(CoreState& state, uint8_t pageIndex) {
    if (pageIndex >= PAGE_COUNT) return;

    const auto previousConfigs = state.pages.activeConfigs;
    state.flushAutoPersist();
    state.pages.setActivePage(pageIndex);
    state.requestMacroWorkspacePersist();
    if (!configsMatch(previousConfigs, state.pages.activeConfigs)) {
        state.configRevision.set(nextMacroConfigRevision(state.configRevision.get()));
    }
    state.statusBar.pageName.set(state.pages.activePageData().name);
    syncRuntimeFromActivePage(state.macros, state.pages);
}

FLASHMEM void MacroWorkflow::switchToTrack(CoreState& state, uint8_t trackIndex) {
    if (trackIndex >= TRACK_COUNT) return;

    const auto previousConfigs = state.pages.activeConfigs;
    state.flushAutoPersist();
    state.setSharedTrackState(state.currentSharedTrackEnabledMask(), trackIndex);
    state.requestMacroWorkspacePersist();
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
    state.requestMacroWorkspacePersist();
    return true;
}

FLASHMEM bool MacroWorkflow::setTrackChannel(CoreState& state, uint8_t channel) {
    if (channel > 15) return false;
    if (state.pages.activeTrackChannel() == channel) return false;

    state.pages.setActiveTrackChannel(channel);
    state.configRevision.set(nextMacroConfigRevision(state.configRevision.get()));
    state.requestMacroWorkspacePersist();
    return true;
}

void MacroWorkflow::setRuntimeValue(core::state::MacroState& macros, uint8_t index, float value) {
    if (index >= MACRO_COUNT) return;
    macros.slots[index].value.set(std::clamp(value, 0.0f, 1.0f));
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

#include "state/macro/MacroWorkflow.hpp"

#include <algorithm>
#include <oc/type/TextFormat.hpp>

#include "state/CoreState.hpp"

namespace core::state::macro {

namespace {

MacroWorkflow::StateRefs makeStateRefs(core::state::CoreState& state) {
    return MacroWorkflow::StateRefs{
        state.macros,
        state.pages,
        state.configRevision,
        state.statusBar,
    };
}

MacroWorkflow::Hooks makeHooks(core::state::CoreState& state) {
    return MacroWorkflow::Hooks{&state};
}

}  // namespace

void MacroWorkflow::Hooks::flushPendingRuntime() const {
    if (coreState) {
        coreState->flush();
    }
}

void MacroWorkflow::Hooks::persistWorkspaceNow() const {
    if (coreState) {
        coreState->persistMacroWorkspace();
    }
}

void MacroWorkflow::syncRuntimeFromActivePage(core::state::MacroState& macros,
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

void MacroWorkflow::syncRuntimeFromActivePage(CoreState& state) {
    syncRuntimeFromActivePage(state.macros, state.pages);
}

void MacroWorkflow::syncRuntimeFromActiveTrack(CoreState& state, uint8_t trackIndex) {
    state.pages.setActiveTrack(trackIndex);
    syncRuntimeFromActivePage(state);
}

void MacroWorkflow::syncActivePageValuesFromRuntime(MacroPagesState& pages,
                                                    const core::state::MacroState& macros) {
    auto& page = pages.activePageData();
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        page.values[i] = std::clamp(macros.slots[i].value.get(), 0.0f, 1.0f);
    }
}

void MacroWorkflow::syncActivePageValuesFromRuntime(CoreState& state) {
    syncActivePageValuesFromRuntime(state.pages, state.macros);
}

void MacroWorkflow::switchToPage(StateRefs state, Hooks hooks, uint8_t pageIndex) {
    if (pageIndex >= PAGE_COUNT) return;

    hooks.flushPendingRuntime();
    state.pages.setActivePage(pageIndex);
    hooks.persistWorkspaceNow();
    state.configRevision.set(state.configRevision.get() + 1);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    syncRuntimeFromActivePage(state.macros, state.pages);
}

void MacroWorkflow::switchToPage(CoreState& state, uint8_t pageIndex) {
    switchToPage(makeStateRefs(state), makeHooks(state), pageIndex);
}

void MacroWorkflow::switchToTrack(StateRefs state, Hooks hooks, uint8_t trackIndex) {
    if (trackIndex >= TRACK_COUNT) return;

    hooks.flushPendingRuntime();
    state.pages.setActiveTrack(trackIndex);
    hooks.persistWorkspaceNow();
    state.configRevision.set(state.configRevision.get() + 1);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    syncRuntimeFromActivePage(state.macros, state.pages);
}

void MacroWorkflow::switchToTrack(CoreState& state, uint8_t trackIndex) {
    switchToTrack(makeStateRefs(state), makeHooks(state), trackIndex);
}

bool MacroWorkflow::setConfig(StateRefs state, Hooks hooks, uint8_t index, uint8_t channel, uint8_t cc) {
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
    state.configRevision.set(state.configRevision.get() + 1);
    hooks.persistWorkspaceNow();
    return true;
}

bool MacroWorkflow::setConfig(CoreState& state, uint8_t index, uint8_t channel, uint8_t cc) {
    return setConfig(makeStateRefs(state), makeHooks(state), index, channel, cc);
}

bool MacroWorkflow::setConfigCc(StateRefs state, Hooks hooks, uint8_t index, uint8_t cc) {
    if (index >= MACRO_COUNT) return false;
    return setConfig(state, hooks, index, state.pages.activeTrackChannel(), cc);
}

bool MacroWorkflow::setConfigCc(CoreState& state, uint8_t index, uint8_t cc) {
    return setConfigCc(makeStateRefs(state), makeHooks(state), index, cc);
}

bool MacroWorkflow::setTrackChannel(StateRefs state, Hooks hooks, uint8_t channel) {
    if (channel > 15) return false;
    if (state.pages.activeTrackChannel() == channel) return false;

    state.pages.setActiveTrackChannel(channel);
    state.configRevision.set(state.configRevision.get() + 1);
    hooks.persistWorkspaceNow();
    return true;
}

bool MacroWorkflow::setTrackChannel(CoreState& state, uint8_t channel) {
    return setTrackChannel(makeStateRefs(state), makeHooks(state), channel);
}

void MacroWorkflow::setRuntimeValue(core::state::MacroState& macros, uint8_t index, float value) {
    if (index >= MACRO_COUNT) return;
    macros.slots[index].value.set(std::clamp(value, 0.0f, 1.0f));
}

void MacroWorkflow::setRuntimeValue(CoreState& state, uint8_t index, float value) {
    setRuntimeValue(state.macros, index, value);
}

float MacroWorkflow::runtimeValue(const core::state::MacroState& macros, uint8_t index) {
    if (index >= MACRO_COUNT) return 0.0f;
    return macros.slots[index].value.get();
}

float MacroWorkflow::runtimeValue(const CoreState& state, uint8_t index) {
    return runtimeValue(state.macros, index);
}

const MacroConfig& MacroWorkflow::activeConfig(const MacroPagesState& pages, uint8_t index) {
    static const MacroConfig defaultConfig{};
    if (index >= MACRO_COUNT) return defaultConfig;
    return pages.activeConfigs[index];
}

const MacroConfig& MacroWorkflow::activeConfig(const CoreState& state, uint8_t index) {
    return activeConfig(state.pages, index);
}

}  // namespace core::state::macro

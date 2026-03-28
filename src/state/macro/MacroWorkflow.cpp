#include "state/macro/MacroWorkflow.hpp"

#include <algorithm>
#include <oc/type/TextFormat.hpp>

#include "state/CoreState.hpp"

namespace core::state::macro {

void MacroWorkflow::syncRuntimeFromActivePage(CoreState& state) {
    const auto& pageData = state.pages.activePageData();
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        char label[16];
        size_t pos = oc::type::text::appendString(label, sizeof(label), 0, "Macro ");
        pos = oc::type::text::appendUnsigned(label, sizeof(label), pos, i + 1);
        oc::type::text::terminate(label, sizeof(label), pos);
        state.macros.slots[i].label.set(label);
        state.macros.slots[i].value.set(std::clamp(pageData.values[i], 0.0f, 1.0f));
    }
}

void MacroWorkflow::syncActivePageValuesFromRuntime(CoreState& state) {
    auto& page = state.pages.activePageData();
    for (uint8_t i = 0; i < MACRO_COUNT; ++i) {
        page.values[i] = std::clamp(state.macros.slots[i].value.get(), 0.0f, 1.0f);
    }
}

void MacroWorkflow::switchToPage(CoreState& state, uint8_t pageIndex) {
    if (pageIndex >= PAGE_COUNT) return;

    state.flush();
    state.pages.setActivePage(pageIndex);
    state.persistMacroWorkspace();
    state.configRevision.set(state.configRevision.get() + 1);
    state.statusBar.pageName.set(state.pages.activePageData().name);
    syncRuntimeFromActivePage(state);
}

bool MacroWorkflow::setConfig(CoreState& state, uint8_t index, uint8_t channel, uint8_t cc) {
    if (index >= MACRO_COUNT) return false;
    if (channel > 15 || cc > 127) return false;

    auto& page = state.pages.activePageData();
    const bool channelChanged = page.channel[index] != channel;
    const bool ccChanged = page.cc[index] != cc;
    if (!channelChanged && !ccChanged) {
        return false;
    }

    page.channel[index] = channel;
    page.cc[index] = cc;
    state.pages.updateActiveConfigs();
    state.persistMacroWorkspace();
    return true;
}

void MacroWorkflow::setRuntimeValue(CoreState& state, uint8_t index, float value) {
    if (index >= MACRO_COUNT) return;
    state.macros.slots[index].value.set(std::clamp(value, 0.0f, 1.0f));
}

float MacroWorkflow::runtimeValue(const CoreState& state, uint8_t index) {
    if (index >= MACRO_COUNT) return 0.0f;
    return state.macros.slots[index].value.get();
}

const MacroConfig& MacroWorkflow::activeConfig(const CoreState& state, uint8_t index) {
    static const MacroConfig defaultConfig{};
    if (index >= MACRO_COUNT) return defaultConfig;
    return state.pages.activeConfigs[index];
}

}  // namespace core::state::macro

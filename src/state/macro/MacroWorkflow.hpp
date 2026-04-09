#pragma once

#include <cstdint>

#include <oc/state/Signal.hpp>

#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroPagesState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::state::macro {

struct MacroWorkflow {
    struct StateRefs {
        core::state::MacroState& macros;
        MacroPagesState& pages;
        oc::state::Signal<uint32_t>& configRevision;
        core::state::StatusBarState& statusBar;
    };

    struct Hooks {
        core::state::CoreState* coreState = nullptr;

        void flushPendingRuntime() const;
        void persistWorkspaceNow() const;
    };

    static void syncRuntimeFromActivePage(core::state::MacroState& macros,
                                          const MacroPagesState& pages);
    static void syncRuntimeFromActivePage(CoreState& state);
    static void syncRuntimeFromActiveTrack(CoreState& state, uint8_t trackIndex);
    static void syncActivePageValuesFromRuntime(MacroPagesState& pages,
                                                const core::state::MacroState& macros);
    static void syncActivePageValuesFromRuntime(CoreState& state);
    static void switchToPage(StateRefs state, Hooks hooks, uint8_t pageIndex);
    static void switchToPage(CoreState& state, uint8_t pageIndex);
    static void switchToTrack(StateRefs state, Hooks hooks, uint8_t trackIndex);
    static void switchToTrack(CoreState& state, uint8_t trackIndex);
    static bool setConfig(StateRefs state, Hooks hooks, uint8_t index, uint8_t channel, uint8_t cc);
    static bool setConfig(CoreState& state, uint8_t index, uint8_t channel, uint8_t cc);
    static bool setConfigCc(StateRefs state, Hooks hooks, uint8_t index, uint8_t cc);
    static bool setConfigCc(CoreState& state, uint8_t index, uint8_t cc);
    static bool setTrackChannel(StateRefs state, Hooks hooks, uint8_t channel);
    static bool setTrackChannel(CoreState& state, uint8_t channel);
    static void setRuntimeValue(core::state::MacroState& macros, uint8_t index, float value);
    static void setRuntimeValue(CoreState& state, uint8_t index, float value);
    static float runtimeValue(const core::state::MacroState& macros, uint8_t index);
    static float runtimeValue(const CoreState& state, uint8_t index);
    static const MacroConfig& activeConfig(const MacroPagesState& pages, uint8_t index);
    static const MacroConfig& activeConfig(const CoreState& state, uint8_t index);
};

}  // namespace core::state::macro

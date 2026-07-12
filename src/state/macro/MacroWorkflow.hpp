#pragma once

#include <cstdint>

#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroPagesState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::state::macro {

/**
 * Macro domain workflow for runtime/page synchronization and config edits.
 *
 * This is the mutation boundary for switching page/track, applying MIDI CC
 * config changes, and syncing runtime values before project snapshots. Narrow
 * overloads are used for pure macro state projection; `CoreState` overloads are
 * kept only where the workflow coordinates flush, project mutation, status,
 * revision, or shared-track side effects.
 */
constexpr uint8_t kMacroConfigDirtyAll = 0xFF;

inline uint32_t nextMacroConfigRevision(uint32_t current, uint8_t dirtyIndex = kMacroConfigDirtyAll) {
    uint32_t generation = ((current >> 8) + 1U) & 0x00FFFFFFU;
    if (generation == 0) {
        generation = 1;
    }
    return (generation << 8) | dirtyIndex;
}

inline bool macroConfigRevisionTargetsAll(uint32_t revision) {
    return static_cast<uint8_t>(revision & 0xFFU) == kMacroConfigDirtyAll;
}

inline int macroConfigRevisionDirtyIndex(uint32_t revision) {
    const uint8_t dirtyIndex = static_cast<uint8_t>(revision & 0xFFU);
    return dirtyIndex < MACRO_COUNT ? static_cast<int>(dirtyIndex) : -1;
}

struct MacroWorkflow {
    static void syncRuntimeFromActivePage(core::state::MacroState& macros,
                                          const MacroPagesState& pages);
    static void switchToPage(CoreState& state, uint8_t pageIndex);
    static void switchToTrack(CoreState& state, uint8_t trackIndex);
    static bool setConfig(CoreState& state, uint8_t index, uint8_t channel, uint8_t cc);
    static bool setTrackChannel(CoreState& state, uint8_t channel);
    static bool activateMacroSlot(core::state::MacroState& macros,
                                  MacroPagesState& pages,
                                  uint8_t index);
    static void setRuntimeValue(core::state::MacroState& macros, uint8_t index, float value);
    static float runtimeValue(const core::state::MacroState& macros, uint8_t index);
    static const MacroConfig& activeConfig(const MacroPagesState& pages, uint8_t index);
};

}  // namespace core::state::macro

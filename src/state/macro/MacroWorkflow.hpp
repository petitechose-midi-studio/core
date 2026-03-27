#pragma once

#include <cstdint>

#include "state/macro/MacroPagesState.hpp"

namespace core::state {
struct CoreState;
}

namespace core::state::macro {

struct MacroWorkflow {
    static void syncRuntimeFromActivePage(CoreState& state);
    static void syncActivePageValuesFromRuntime(CoreState& state);
    static void switchToPage(CoreState& state, uint8_t pageIndex);
    static bool setConfig(CoreState& state, uint8_t index, uint8_t channel, uint8_t cc);
    static void setRuntimeValue(CoreState& state, uint8_t index, float value);
    static float runtimeValue(const CoreState& state, uint8_t index);
    static const MacroConfig& activeConfig(const CoreState& state, uint8_t index);
};

}  // namespace core::state::macro

#pragma once

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::context::standalone {

inline void prepareMacroViewActivation(core::state::CoreState& state) {
    // Macro-view activation owns runtime/status resync in the standalone lifecycle seam.
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state);
    state.statusBar.pageName.set(state.pages.activePageData().name);
}

}  // namespace core::context::standalone

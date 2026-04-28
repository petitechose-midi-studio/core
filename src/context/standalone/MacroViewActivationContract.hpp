#pragma once

#include "state/CoreState.hpp"
#include "state/macro/MacroWorkflow.hpp"

namespace core::context::standalone {

/**
 * Macro view activation contract for standalone lifecycle.
 *
 * Entering the macro view must refresh runtime macro values and the status-page
 * label from the active persisted macro page before input handlers resume.
 */
inline void prepareMacroViewActivation(core::state::CoreState& state) {
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state);
    state.statusBar.pageName.set(state.pages.activePageData().name);
}

}  // namespace core::context::standalone

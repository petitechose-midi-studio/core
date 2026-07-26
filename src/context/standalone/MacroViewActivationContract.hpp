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
void prepareMacroViewActivation(core::state::CoreState& state);

}  // namespace core::context::standalone

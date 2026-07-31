#include "context/standalone/MacroViewActivationContract.hpp"

#include <config/PlatformCompat.hpp>

namespace core::context::standalone {

FLASHMEM void prepareMacroViewActivation(core::state::CoreState& state) {
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
        state.macros,
        state.pages
    );
}

}  // namespace core::context::standalone

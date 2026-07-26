#include "context/standalone/MacroViewActivationContract.hpp"

#include <config/PlatformCompat.hpp>

namespace core::context::standalone {

FLASHMEM void prepareMacroViewActivation(core::state::CoreState& state) {
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(
        state.macros,
        state.pages
    );
    state.statusBar.pageName.set(state.pages.activePageData().name);
}

}  // namespace core::context::standalone

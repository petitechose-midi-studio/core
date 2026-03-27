#include "ui/view/MacroViewModelBuilder.hpp"

namespace core::ui {

TopBarProps buildMacroTopBarProps(const core::state::CoreState& coreState) {
    return {
        .pageName = coreState.statusBar.pageName.get(),
    };
}

MacroViewFrameState buildMacroViewFrameState(const core::state::CoreState& coreState) {
    MacroViewFrameState frame;

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto& config = core::state::macro::MacroWorkflow::activeConfig(coreState, i);
        frame.macros[i] = {
            .value = core::state::macro::MacroWorkflow::runtimeValue(coreState, i),
            .channel = config.channel,
            .cc = config.cc,
        };
    }

    return frame;
}

}  // namespace core::ui

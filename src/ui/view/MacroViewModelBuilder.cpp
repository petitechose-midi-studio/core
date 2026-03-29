#include "ui/view/MacroViewModelBuilder.hpp"

namespace core::ui {

TopBarProps buildMacroTopBarProps(const MacroViewModelSource& source) {
    return {
        .pageName = source.statusBar.pageName.get(),
    };
}

MacroViewFrameState buildMacroViewFrameState(const MacroViewModelSource& source) {
    MacroViewFrameState frame;

    for (uint8_t i = 0; i < Config::MACRO_COUNT; ++i) {
        const auto& config = source.pages.activeConfigs[i];
        frame.macros[i] = {
            .value = source.macros.slots[i].value.get(),
            .channel = config.channel,
            .cc = config.cc,
        };
    }

    return frame;
}

}  // namespace core::ui

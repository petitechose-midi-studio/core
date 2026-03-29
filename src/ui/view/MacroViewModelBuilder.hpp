#pragma once

#include <array>

#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "ui/topbar/TopBar.hpp"

namespace core::ui {

struct MacroViewModelSource {
    const core::state::MacroState& macros;
    const core::state::macro::MacroPagesState& pages;
    const core::state::StatusBarState& statusBar;
};

struct MacroWidgetProps {
    float value = 0.5f;
    uint8_t channel = 0;
    uint8_t cc = 0;
};

struct MacroViewFrameState {
    std::array<MacroWidgetProps, Config::MACRO_COUNT> macros{};
};

TopBarProps buildMacroTopBarProps(const MacroViewModelSource& source);
MacroViewFrameState buildMacroViewFrameState(const MacroViewModelSource& source);

}  // namespace core::ui

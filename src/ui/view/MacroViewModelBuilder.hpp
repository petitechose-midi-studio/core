#pragma once

#include <array>

#include "state/CoreState.hpp"
#include "ui/topbar/TopBar.hpp"

namespace core::ui {

struct MacroWidgetProps {
    float value = 0.5f;
    uint8_t channel = 0;
    uint8_t cc = 0;
};

struct MacroViewFrameState {
    std::array<MacroWidgetProps, Config::MACRO_COUNT> macros{};
};

TopBarProps buildMacroTopBarProps(const core::state::CoreState& coreState);
MacroViewFrameState buildMacroViewFrameState(const core::state::CoreState& coreState);

}  // namespace core::ui

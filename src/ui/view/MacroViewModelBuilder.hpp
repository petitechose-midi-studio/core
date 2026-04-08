#pragma once

#include <array>

#include "state/MacroState.hpp"
#include "state/StatusBarState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "ui/macro/MacroBottomControls.hpp"
#include "ui/macro/MacroHeaderBar.hpp"
#include "ui/macro/MacroPropertyStrip.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui {

struct MacroViewModelSource {
    const core::state::MacroState& macros;
    const core::state::macro::MacroPagesState& pages;
    const core::state::macro::MacroUiState& macroUi;
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

MacroHeaderBarProps buildMacroHeaderBarProps(const MacroViewModelSource& source);
MacroBottomControlsProps buildMacroBottomControlsProps(const MacroViewModelSource& source);
MacroPropertyStripProps buildMacroPropertyStripProps(const MacroViewModelSource& source);
ContextActionStripProps buildMacroLeftActionStripProps(const MacroViewModelSource& source);
ContextActionStripProps buildMacroBottomActionStripProps(const MacroViewModelSource& source);
MacroViewFrameState buildMacroViewFrameState(const MacroViewModelSource& source);

}  // namespace core::ui

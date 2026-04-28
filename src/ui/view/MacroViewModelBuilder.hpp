#pragma once

#include <array>

#include <oc/state/Signal.hpp>

#include "state/MacroState.hpp"
#include "state/StructureSelectionState.hpp"
#include "state/StatusBarState.hpp"
#include "state/StructureClipboardState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"
#include "ui/macro/MacroBottomControls.hpp"
#include "ui/macro/MacroHeaderBar.hpp"
#include "ui/macro/MacroPropertyStrip.hpp"
#include "ui/common/TrackNavigationStrip.hpp"
#include "ui/strip/ContextActionStrip.hpp"

namespace core::ui {

/**
 * Projects macro domain state into immutable view props.
 *
 * Builders in this header read MacroState/MacroPagesState/UI state and produce
 * widget props only. They do not mutate state or touch LVGL objects.
 */
struct MacroViewModelSource {
    const core::state::MacroState& macros;
    const core::state::macro::MacroPagesState& pages;
    const core::state::macro::MacroUiState& macroUi;
    const core::state::TrackNavigationState& trackNavigation;
    const oc::state::Signal<
        core::state::StructureNavigationFocus,
        core::state::kStructureNavigationFocusMaxSubscribers>& navigationFocus;
    const oc::state::Signal<uint8_t, 8>& sharedTrackActive;
    const oc::state::Signal<uint16_t, 16>& sharedTrackEnabledMask;
    const core::state::StructureClipboardState& structureClipboard;
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

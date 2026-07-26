#pragma once

#include <cstdint>

#include "state/StructureClipboardState.hpp"
#include "state/StructureNavigationState.hpp"
#include "state/TrackNavigationState.hpp"
#include "state/macro/MacroInteractionPolicy.hpp"
#include "state/macro/MacroPagesState.hpp"
#include "state/macro/MacroUiState.hpp"

namespace core::state::macro {

struct MacroInteractionContextSource {
    const MacroPagesState& pages;
    const MacroUiState& macroUi;
    const core::state::TrackNavigationState& trackNavigation;
    const core::state::StructureClipboardState& structureClipboard;
    core::state::StructureNavigationFocus navigationFocus =
        core::state::StructureNavigationFocus::PAGE;
    uint16_t enabledTrackMask = MacroPagesState::DEFAULT_TRACK_ENABLED_MASK;
    bool blockingOverlay = false;
    bool slotPropertySelecting = false;
};

core::state::StructureNavigationFocus effectiveMacroNavigationFocus(
    core::state::StructureNavigationFocus requestedFocus
);
bool macroInteractionPreviewingAddSlot(const MacroInteractionContextSource& source);
bool macroInteractionCanPasteStructure(const MacroInteractionContextSource& source);
bool macroInteractionCanRemoveStructure(const MacroInteractionContextSource& source);
MacroInteractionContext buildMacroInteractionContext(
    const MacroInteractionContextSource& source
);

}  // namespace core::state::macro

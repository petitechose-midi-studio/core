#include "state/macro/MacroUiState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

FLASHMEM MacroUiState::MacroUiState() = default;
FLASHMEM MacroUiState::~MacroUiState() = default;

FLASHMEM void MacroUiState::reset() {
    clutchActive.set(false);
    activeProperty.set(MacroPerformanceProperty::VALUE);
    quickControlsSelecting.set(false);
    focusedQuickControl.set(MacroQuickControlItem::GLOBAL_CHANNEL);
    clutchPreviewTrackChannel.set(0);
    quickControlGlobalChannel.set(0);
    ccOffset.set(0);
    previewAddPageSlot.set(false);
    previewPageIndex.set(0);
    pageHold.clear();
    pageSelection.reset(core::state::StructureSelectionScope::PAGE);
}

}  // namespace core::state::macro

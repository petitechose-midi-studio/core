#include "state/macro/MacroUiState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

FLASHMEM MacroUiState::MacroUiState() = default;
FLASHMEM MacroUiState::~MacroUiState() = default;

FLASHMEM void MacroUiState::reset() {
    clutchActive.set(false);
    activeProperty.set(MacroPerformanceProperty::VALUE);
    automationManualOverrideMask.set(0);
    focusedMacroSlot.set(0);
    previewAddPageSlot.set(false);
    previewPageIndex.set(0);
    pageHold.clear();
    pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    automationRecording.reset();
    automationRecordingStatus.set(MacroAutomationRecordingStatus::IDLE);
}

}  // namespace core::state::macro

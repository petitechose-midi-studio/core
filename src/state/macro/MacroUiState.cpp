#include "state/macro/MacroUiState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

FLASHMEM MacroUiState::MacroUiState() = default;
FLASHMEM MacroUiState::~MacroUiState() = default;

FLASHMEM void MacroUiState::resetInteraction() {
    // Beginning a recording temporarily removes Manual so the live gesture can
    // own the value. A context teardown is a cancellation, not a Resume.
    if (automationRecording.active && automationRecording.restoreManualOnFailure) {
        (void)manualOverrides.activate(
            automationRecording.address,
            automationRecording.previousManualValue
        );
    }
    clutchActive.set(false);
    activeProperty.set(MacroPerformanceProperty::VALUE);
    automationManualOverrideMask.set(0);
    focusedMacroSlot.set(0);
    previewAddPageSlot.set(false);
    previewPageIndex.set(0);
    pageHold.clear();
    pageSelection.reset(core::state::StructureSelectionScope::PAGE);
    selectionDeleteGuard.set({});
    selectionDeleteFeedback.set({});
    automationRecording.reset();
    automationRecordingStatus.set(MacroAutomationRecordingStatus::IDLE);
}

FLASHMEM void MacroUiState::resetProjectRuntime() {
    manualOverrides.clearProjectRuntime();
    automationManualOverrideMask.set(0);
}

FLASHMEM void MacroUiState::reset() {
    resetInteraction();
    resetProjectRuntime();
}

FLASHMEM void MacroUiState::refreshManualOverrideMask(uint8_t track, uint8_t page) {
    uint16_t mask = 0;
    if (track < TRACK_COUNT && page < PAGE_COUNT) {
        for (uint8_t macro = 0; macro < MACRO_COUNT; ++macro) {
            if (manualOverrides.activeFor(MacroAutomationSlotAddress{
                    .track = track,
                    .page = page,
                    .macro = macro,
                })) {
                mask = static_cast<uint16_t>(mask | static_cast<uint16_t>(1U << macro));
            }
        }
    }
    automationManualOverrideMask.set(mask);
}

}  // namespace core::state::macro

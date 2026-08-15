#include "state/DeviceSettingsState.hpp"
#include "state/PatternPitchSettingsState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM void DeviceSettingsValueSelectorState::reset() {
    visible.set(false);
    selectedIndex.set(0);
    editingRow.set(0);
}

FLASHMEM void DeviceSettingsState::reset() {
    visible.set(false);
    focusedRow.set(0);
    flowPhase.set(DeviceSettingsFlowPhase::CLOSED);
    selector.reset();
}

FLASHMEM void DeviceSettingsState::openView() {
    reset();
    visible.set(true);
    flowPhase.set(DeviceSettingsFlowPhase::VIEW);
}

FLASHMEM void DeviceSettingsState::closeView() {
    reset();
}

FLASHMEM void DeviceSettingsState::openSelector(
    uint8_t row,
    int selectedIndex
) {
    visible.set(true);
    selector.visible.set(true);
    selector.editingRow.set(row);
    selector.selectedIndex.set(selectedIndex);
    flowPhase.set(DeviceSettingsFlowPhase::VALUE_SELECTOR);
}

FLASHMEM void DeviceSettingsState::closeSelector() {
    selector.reset();
    flowPhase.set(visible.get() ? DeviceSettingsFlowPhase::VIEW
                                : DeviceSettingsFlowPhase::CLOSED);
}

FLASHMEM void PatternPitchSettingsValueSelectorState::reset() {
    visible.set(false);
    editingRow.set(0);
    selectedIndex.set(0);
}

FLASHMEM void PatternPitchSettingsState::reset() {
    visible.set(false);
    focusedRow.set(0);
    flowPhase.set(PatternPitchSettingsFlowPhase::CLOSED);
    selector.reset();
}

FLASHMEM void PatternPitchSettingsState::openOverlay() {
    reset();
    visible.set(true);
    flowPhase.set(PatternPitchSettingsFlowPhase::OVERLAY);
}

FLASHMEM void PatternPitchSettingsState::closeOverlay() {
    reset();
}

FLASHMEM void PatternPitchSettingsState::openSelector(
    uint8_t row,
    int selected
) {
    selector.editingRow.set(row);
    selector.selectedIndex.set(selected);
    selector.visible.set(true);
    flowPhase.set(PatternPitchSettingsFlowPhase::VALUE_SELECTOR);
}

FLASHMEM void PatternPitchSettingsState::closeSelector() {
    selector.reset();
    flowPhase.set(visible.get() ? PatternPitchSettingsFlowPhase::OVERLAY
                                : PatternPitchSettingsFlowPhase::CLOSED);
}

}  // namespace core::state

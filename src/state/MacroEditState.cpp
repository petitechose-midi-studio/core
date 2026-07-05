#include "state/MacroEditState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM void MacroEditState::ValueSelectorState::reset() {
    visible.set(false);
    editingRow.set(0);
    selectedIndex.set(0);
}

FLASHMEM void MacroEditState::MacroSelectorState::reset() {
    visible.set(false);
    selectedIndex.set(0);
}

FLASHMEM void MacroEditState::reset() {
    visible.set(false);
    automationVisible.set(false);
    flowPhase.set(MacroEditFlowPhase::CLOSED);
    editingIndex.set(0);
    tempChannel.set(0);
    tempCC.set(0);
    focusedRow.set(0);
    selector.reset();
    macroSelector.reset();
    automationFocusedRow.set(0);
    openedByMacroIndex = 0;
    openedAtMs = 0;
    pendingOpenReleaseDecision = false;
}

FLASHMEM void MacroEditState::openEditor(
    uint8_t index,
    uint8_t channel,
    uint8_t cc,
    uint32_t openedAt
) {
    reset();
    visible.set(true);
    flowPhase.set(MacroEditFlowPhase::EDIT);
    editingIndex.set(index);
    tempChannel.set(channel);
    tempCC.set(cc);
    focusedRow.set(0);
    openedByMacroIndex = index;
    openedAtMs = openedAt;
    pendingOpenReleaseDecision = true;
}

FLASHMEM void MacroEditState::closeEditor() {
    reset();
}

FLASHMEM void MacroEditState::openValueSelector(uint8_t row, int selectedIndex) {
    visible.set(true);
    selector.visible.set(true);
    selector.editingRow.set(row);
    selector.selectedIndex.set(selectedIndex);
    flowPhase.set(MacroEditFlowPhase::VALUE_SELECTOR);
}

FLASHMEM void MacroEditState::closeValueSelector() {
    selector.reset();
    flowPhase.set(visible.get() ? MacroEditFlowPhase::EDIT
                                : MacroEditFlowPhase::CLOSED);
}

FLASHMEM void MacroEditState::openPageSelector() {
    visible.set(true);
    flowPhase.set(MacroEditFlowPhase::PAGE_SELECTOR);
}

FLASHMEM void MacroEditState::closePageSelector() {
    flowPhase.set(visible.get() ? MacroEditFlowPhase::EDIT
                                : MacroEditFlowPhase::CLOSED);
}

FLASHMEM void MacroEditState::openTargetSelector(int selectedIndex) {
    visible.set(true);
    macroSelector.visible.set(true);
    macroSelector.selectedIndex.set(selectedIndex);
    flowPhase.set(MacroEditFlowPhase::TARGET_SELECTOR);
}

FLASHMEM void MacroEditState::closeTargetSelector() {
    macroSelector.reset();
    flowPhase.set(visible.get() ? MacroEditFlowPhase::EDIT
                                : MacroEditFlowPhase::CLOSED);
}

FLASHMEM void MacroEditState::openAutomation() {
    visible.set(true);
    automationVisible.set(true);
    automationFocusedRow.set(0);
    flowPhase.set(MacroEditFlowPhase::AUTOMATION);
}

FLASHMEM void MacroEditState::closeAutomation() {
    automationVisible.set(false);
    automationFocusedRow.set(0);
    flowPhase.set(visible.get() ? MacroEditFlowPhase::EDIT
                                : MacroEditFlowPhase::CLOSED);
}

FLASHMEM void MacroEditState::loadActiveConfig(uint8_t index, uint8_t channel, uint8_t cc) {
    editingIndex.set(index);
    tempChannel.set(channel);
    tempCC.set(cc);
}

FLASHMEM bool MacroEditState::consumeOpeningReleaseDecision(
    uint8_t macroIndex,
    uint32_t nowMs,
    uint32_t quickReleaseWindowMs
) {
    if (!visible.get()) return false;
    if (!pendingOpenReleaseDecision) return false;
    if (macroIndex != openedByMacroIndex) return false;

    pendingOpenReleaseDecision = false;
    return (nowMs - openedAtMs) >= quickReleaseWindowMs;
}

}  // namespace core::state

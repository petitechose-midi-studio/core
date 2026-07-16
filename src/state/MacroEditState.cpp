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

FLASHMEM void MacroEditState::ConversionPreviewState::reset() {
    policy = core::state::macro::MacroAutomationConversionPolicy::MEAN;
    plan = {};
    revision.set(revision.get() + 1U);
}

FLASHMEM void MacroEditState::ConversionPreviewState::setPlan(
    const core::state::macro::MacroAutomationConversionPlan& next
) {
    policy = next.policy;
    plan = next;
    revision.set(revision.get() + 1U);
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
    modulationFocusedRow.set(0);
    conversionPreview.reset();
    contextGuard.set({});
    contextFeedback.set({});
    contextButton.set(MacroContextButton::NONE);
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

FLASHMEM void MacroEditState::openModulation(uint8_t focusedRow) {
    visible.set(true);
    automationVisible.set(true);
    modulationFocusedRow.set(focusedRow);
    flowPhase.set(MacroEditFlowPhase::MODULATION);
}

FLASHMEM void MacroEditState::closeModulation() {
    automationVisible.set(false);
    modulationFocusedRow.set(0);
    flowPhase.set(visible.get() ? MacroEditFlowPhase::EDIT
                                : MacroEditFlowPhase::CLOSED);
}

FLASHMEM void MacroEditState::openModulatorCreate() {
    visible.set(true);
    automationVisible.set(true);
    modulationFocusedRow.set(0);
    flowPhase.set(MacroEditFlowPhase::MODULATOR_CREATE);
}

FLASHMEM void MacroEditState::closeModulatorCreate(uint8_t focusedRow) {
    modulationFocusedRow.set(focusedRow);
    flowPhase.set(visible.get() ? MacroEditFlowPhase::MODULATION
                                : MacroEditFlowPhase::CLOSED);
}

FLASHMEM void MacroEditState::openLfoAudition() {
    visible.set(true);
    automationVisible.set(true);
    modulationFocusedRow.set(0);
    flowPhase.set(MacroEditFlowPhase::LFO_AUDITION);
}

FLASHMEM void MacroEditState::cancelLfoAudition(uint8_t focusedRow) {
    modulationFocusedRow.set(focusedRow);
    flowPhase.set(visible.get() ? MacroEditFlowPhase::MODULATION
                                : MacroEditFlowPhase::CLOSED);
}

FLASHMEM void MacroEditState::openModulatorPicker(int selectedIndex) {
    visible.set(true);
    automationVisible.set(true);
    macroSelector.selectedIndex.set(selectedIndex);
    flowPhase.set(MacroEditFlowPhase::MODULATOR_PICKER);
}

FLASHMEM void MacroEditState::closeModulatorPicker(uint8_t focusedRow) {
    modulationFocusedRow.set(focusedRow);
    flowPhase.set(visible.get() ? MacroEditFlowPhase::MODULATION
                                : MacroEditFlowPhase::CLOSED);
}

FLASHMEM void MacroEditState::openExistingModulatorAudition() {
    visible.set(true);
    automationVisible.set(true);
    modulationFocusedRow.set(1);
    flowPhase.set(MacroEditFlowPhase::EXISTING_MODULATOR_AUDITION);
}

FLASHMEM void MacroEditState::cancelExistingModulatorAudition() {
    modulationFocusedRow.set(0);
    flowPhase.set(visible.get() ? MacroEditFlowPhase::MODULATOR_PICKER
                                : MacroEditFlowPhase::CLOSED);
}

FLASHMEM void MacroEditState::applyModulatorAudition() {
    automationVisible.set(false);
    modulationFocusedRow.set(0);
    flowPhase.set(visible.get() ? MacroEditFlowPhase::EDIT
                                : MacroEditFlowPhase::CLOSED);
}

FLASHMEM void MacroEditState::openConvertPreview(
    const core::state::macro::MacroAutomationConversionPlan& plan
) {
    visible.set(true);
    automationVisible.set(true);
    conversionPreview.setPlan(plan);
    flowPhase.set(MacroEditFlowPhase::CONVERT_PREVIEW);
}

FLASHMEM void MacroEditState::closeConvertPreview() {
    flowPhase.set(visible.get() ? MacroEditFlowPhase::MODULATION
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

#include "state/DataManagerState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state {

FLASHMEM void DataManagerDialogState::reset() {
    visible.set(false);
    mode.set(DataManagerDialogMode::ASSIGN_SHORTCUT);
    selectedIndex.set(0);
    editingShortcutRow.set(0);
}

FLASHMEM void DataManagerState::resetSession(DataManagerContext activeContext) {
    visible.set(false);
    focusedRow.set(0);
    context.set(activeContext);
    flowPhase.set(DataManagerFlowPhase::CLOSED);
    pendingCommand.set(DataManagerCommand::NONE);
    pendingSlot.set(0);
    pendingSetLoadMode.set(DataManagerSetLoadMode::REPLACE);
    dialog.reset();
}

FLASHMEM void DataManagerState::openSession(DataManagerContext activeContext) {
    resetSession(activeContext);
    visible.set(true);
    flowPhase.set(DataManagerFlowPhase::MANAGER);
}

FLASHMEM void DataManagerState::closeSession() {
    visible.set(false);
    flowPhase.set(DataManagerFlowPhase::CLOSED);
    clearPendingCommand();
    dialog.reset();
    feedback.set("");
}

FLASHMEM void DataManagerState::showDialog(DataManagerDialogMode mode,
                                           int selectedIndex,
                                           uint8_t editingShortcutRow) {
    visible.set(true);
    dialog.mode.set(mode);
    dialog.selectedIndex.set(selectedIndex);
    dialog.editingShortcutRow.set(editingShortcutRow);
    dialog.visible.set(true);
    flowPhase.set(dataManagerFlowPhaseForDialogMode(mode));
}

FLASHMEM void DataManagerState::closeDialog() {
    dialog.reset();
    flowPhase.set(visible.get() ? DataManagerFlowPhase::MANAGER : DataManagerFlowPhase::CLOSED);
}

FLASHMEM void DataManagerState::clearPendingCommand() {
    pendingCommand.set(DataManagerCommand::NONE);
    pendingSetLoadMode.set(DataManagerSetLoadMode::REPLACE);
}

FLASHMEM DataManagerCommand DataManagerState::shortcutForSide(
    DataManagerShortcutSide side
) const {
    const bool left = side == DataManagerShortcutSide::LEFT;
    if (context.get() == DataManagerContext::MACRO) {
        return left ? macroShortcutLeft.get() : macroShortcutRight.get();
    }
    return left ? seqShortcutLeft.get() : seqShortcutRight.get();
}

FLASHMEM DataManagerCommand DataManagerState::shortcutForRow(uint8_t row) const {
    return shortcutForSide((row == 0U) ? DataManagerShortcutSide::LEFT
                                       : DataManagerShortcutSide::RIGHT);
}

}  // namespace core::state

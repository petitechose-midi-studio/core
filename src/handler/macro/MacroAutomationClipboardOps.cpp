#include "handler/macro/MacroAutomationClipboardOps.hpp"

#include <config/PlatformCompat.hpp>

namespace core::handler::macro::automation_clipboard_ops {

FLASHMEM bool hasFirstClipboardAutomation(
    const core::state::StructureClipboardState& clipboard
) {
    if (!clipboard.hasMacroAutomation() ||
        !clipboard.macroAutomationSet ||
        !clipboard.macroAutomationSet->valid ||
        clipboard.macroAutomationSet->count == 0) {
        return false;
    }

    const auto& entry = clipboard.macroAutomationSet->entries[0];
    return entry.valid && entry.state.automation.active;
}

FLASHMEM bool copySlotAutomationToClipboard(
    const core::state::macro::MacroAutomationBankState& bank,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
) {
    const auto* slot = core::state::macro::macroAutomationFindSlot(bank, address);
    if (slot == nullptr || !slot->automation.active) return false;

    clipboard.storeMacroAutomation(bank, *slot);
    return true;
}

FLASHMEM bool pasteFirstClipboardAutomationToSlot(
    core::state::macro::MacroAutomationBankState& bank,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
) {
    if (!hasFirstClipboardAutomation(clipboard)) return false;

    const auto& entry = clipboard.macroAutomationSet->entries[0];
    const bool hadSlot =
        core::state::macro::macroAutomationFindSlot(bank, address) != nullptr;
    auto* slot = core::state::macro::macroAutomationGetOrCreateSlot(bank, address);
    if (slot == nullptr) return false;

    if (!core::state::macro::macroAutomationCopySlotState(
            bank,
            *slot,
            clipboard.macroAutomationSet->pointPool,
            entry.state
        )) {
        if (!hadSlot) {
            core::state::macro::macroAutomationClearSlot(bank, address);
        }
        return false;
    }

    return true;
}

}  // namespace core::handler::macro::automation_clipboard_ops

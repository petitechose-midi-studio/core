#pragma once

#include "state/StructureClipboardState.hpp"
#include "state/macro/MacroAutomationState.hpp"

namespace core::handler::macro::automation_clipboard_ops {

bool hasFirstClipboardAutomation(const core::state::StructureClipboardState& clipboard);

bool copySlotAutomationToClipboard(
    const core::state::macro::MacroAutomationBankState& bank,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
);

bool pasteFirstClipboardAutomationToSlot(
    core::state::macro::MacroAutomationBankState& bank,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
);

}  // namespace core::handler::macro::automation_clipboard_ops

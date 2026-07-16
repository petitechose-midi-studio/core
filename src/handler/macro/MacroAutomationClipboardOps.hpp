#pragma once

#include "state/StructureClipboardState.hpp"
#include "state/macro/MacroAutomationState.hpp"

namespace core::handler::macro::automation_clipboard_ops {

enum class MacroTypedPasteStatus : uint8_t {
    READY_FREE = 0,
    OVERWRITE_REQUIRED,
    EMPTY_CLIPBOARD,
    INVALID_TARGET,
    INVALID_PAYLOAD,
    SLOT_CAPACITY_EXHAUSTED,
    POINT_POOL_EXHAUSTED,
};

struct MacroTypedPastePreflight {
    MacroTypedPasteStatus status = MacroTypedPasteStatus::EMPTY_CLIPBOARD;
    uint16_t requiredPointCount = 0;
    uint16_t reclaimablePointCount = 0;
    uint16_t freePointCount = 0;

    [[nodiscard]] bool actionable() const {
        return status == MacroTypedPasteStatus::READY_FREE ||
               status == MacroTypedPasteStatus::OVERWRITE_REQUIRED;
    }
    [[nodiscard]] bool requiresOverwrite() const {
        return status == MacroTypedPasteStatus::OVERWRITE_REQUIRED;
    }
};

bool hasFirstClipboardAutomation(const core::state::StructureClipboardState& clipboard);

bool copySlotAutomationToClipboard(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
);

bool pasteFirstClipboardAutomationToSlot(
    core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
);

MacroTypedPastePreflight preflightAutomationPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
);

bool pasteAutomationFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
);

bool copyDestinationToClipboard(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
);

MacroTypedPastePreflight preflightDestinationPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
);

bool pasteDestinationFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
);

bool copySlotToClipboard(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
);

MacroTypedPastePreflight preflightSlotPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
);

bool pasteSlotFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
);

bool copyModulationToClipboard(
    const core::state::modulation::ProjectControlState& control,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
);

MacroTypedPastePreflight preflightModulationPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
);

bool pasteModulationFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
);

}  // namespace core::handler::macro::automation_clipboard_ops

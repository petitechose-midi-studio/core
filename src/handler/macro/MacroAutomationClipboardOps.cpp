#include "handler/macro/MacroAutomationClipboardOps.hpp"

#include <config/PlatformCompat.hpp>

namespace core::handler::macro::automation_clipboard_ops {

namespace {

bool curvePayloadValid(
    const core::state::macro::MacroAutomationCurveRef& curve,
    const core::state::macro::MacroAutomationPointPool& pool
) {
    if (!curve.active) return curve.pointCount == 0;
    if (curve.pointCount == 0 || curve.pointOffset >= pool.used) return false;
    return static_cast<uint32_t>(curve.pointOffset) + curve.pointCount <= pool.used;
}

bool slotPayloadValid(const core::state::MacroAutomationClipboard& payload) {
    if (!payload.valid || payload.count != 1 || !payload.entries[0].valid) {
        return false;
    }
    const auto& state = payload.entries[0].state;
    return curvePayloadValid(state.automation, payload.pointPool) &&
           curvePayloadValid(state.modulation, payload.pointPool) &&
           core::state::macro::macroAutomationSlotStateValidForMutation(
               state,
               payload.pointPool
           );
}

MacroTypedPastePreflight preflightCapacity(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    uint16_t required,
    uint16_t reclaimable,
    bool needsSlotEntry,
    bool overwrite
) {
    MacroTypedPastePreflight plan{};
    plan.requiredPointCount = required;
    plan.reclaimablePointCount = reclaimable;
    plan.freePointCount = static_cast<uint16_t>(
        core::state::macro::MACRO_AUTOMATION_POINT_POOL_CAPACITY -
        pages.automation.pointPool.used
    );
    if (needsSlotEntry &&
        core::state::macro::macroAutomationFindSlot(pages.automation, address) == nullptr &&
        pages.automation.entryCount >=
            core::state::macro::MACRO_AUTOMATION_SLOT_CAPACITY) {
        plan.status = MacroTypedPasteStatus::SLOT_CAPACITY_EXHAUSTED;
        return plan;
    }
    if (static_cast<uint32_t>(required) >
        static_cast<uint32_t>(plan.freePointCount) + reclaimable) {
        plan.status = MacroTypedPasteStatus::POINT_POOL_EXHAUSTED;
        return plan;
    }
    plan.status = overwrite ? MacroTypedPasteStatus::OVERWRITE_REQUIRED
                            : MacroTypedPasteStatus::READY_FREE;
    return plan;
}

}  // namespace

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
    if (slot == nullptr || !core::state::macro::macroCurveStored(slot->automation)) {
        return false;
    }

    if (!clipboard.storeMacroAutomation(bank, *slot) ||
        !clipboard.macroAutomationSet) {
        return false;
    }
    clipboard.macroAutomationSet->sourceTrack = address.track;
    clipboard.macroAutomationSet->sourcePage = address.page;
    clipboard.macroAutomationSet->sourceMacro = address.macro;
    clipboard.macroAutomationSet->sourceMacroActive = true;
    clipboard.macroAutomationSet->sourceSlotPresent = true;
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

    if (!core::state::macro::macroAutomationCopyAutomationState(
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

FLASHMEM MacroTypedPastePreflight preflightAutomationPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
) {
    MacroTypedPastePreflight rejected{};
    if (!core::state::macro::macroAutomationAddressValid(address) ||
        !pages.pageData(address.track, address.page).isMacroActive(address.macro)) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    if (!clipboard.hasMacroAutomation() || !clipboard.macroAutomationSet) {
        rejected.status = MacroTypedPasteStatus::EMPTY_CLIPBOARD;
        return rejected;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    if (!slotPayloadValid(payload) ||
        !core::state::macro::macroCurveStored(payload.entries[0].state.automation)) {
        rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return rejected;
    }
    const auto* target = core::state::macro::macroAutomationFindSlot(
        pages.automation,
        address
    );
    const uint16_t required = payload.entries[0].state.automation.pointCount;
    const uint16_t reclaimable = target != nullptr
        ? core::state::macro::macroAutomationStoredPointCount(
              target->automation,
              pages.automation.pointPool
          )
        : 0;
    const bool populated = target != nullptr &&
        core::state::macro::macroCurveStored(target->automation);
    return preflightCapacity(
        pages,
        address,
        required,
        reclaimable,
        true,
        populated
    );
}

FLASHMEM bool pasteAutomationFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
) {
    const auto plan = preflightAutomationPaste(pages, address, clipboard);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    auto* target = core::state::macro::macroAutomationGetOrCreateSlot(
        pages.automation,
        address
    );
    if (target == nullptr) return false;
    const auto& payload = *clipboard.macroAutomationSet;
    return core::state::macro::macroAutomationCopyAutomationState(
        pages.automation,
        *target,
        payload.pointPool,
        payload.entries[0].state
    );
}

FLASHMEM bool copyDestinationToClipboard(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
) {
    return clipboard.storeMacroDestination(pages, address);
}

FLASHMEM MacroTypedPastePreflight preflightDestinationPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
) {
    MacroTypedPastePreflight plan{};
    if (!core::state::macro::macroAutomationAddressValid(address) ||
        !pages.pageData(address.track, address.page).isMacroActive(address.macro)) {
        plan.status = MacroTypedPasteStatus::INVALID_TARGET;
        return plan;
    }
    if (!clipboard.hasMacroDestination() || !clipboard.macroAutomationSet) {
        plan.status = MacroTypedPasteStatus::EMPTY_CLIPBOARD;
        return plan;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    if (!slotPayloadValid(payload) || payload.sourceCc > 127) {
        plan.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return plan;
    }
    if (pages.pageData(address.track, address.page).cc[address.macro] ==
        payload.sourceCc) {
        plan.status = MacroTypedPasteStatus::INVALID_TARGET;
        return plan;
    }
    plan.status = MacroTypedPasteStatus::OVERWRITE_REQUIRED;
    return plan;
}

FLASHMEM bool pasteDestinationFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
) {
    const auto plan = preflightDestinationPaste(pages, address, clipboard);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    auto& page = pages.pageData(address.track, address.page);
    page.cc[address.macro] = clipboard.macroAutomationSet->sourceCc;
    if (pages.currentActiveTrack() == address.track &&
        pages.currentActivePage() == address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

FLASHMEM bool copySlotToClipboard(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
) {
    return clipboard.storeMacroSlot(pages, address);
}

FLASHMEM MacroTypedPastePreflight preflightSlotPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
) {
    MacroTypedPastePreflight rejected{};
    if (!core::state::macro::macroAutomationAddressValid(address)) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    if (!clipboard.hasMacroSlot() || !clipboard.macroAutomationSet) {
        rejected.status = MacroTypedPasteStatus::EMPTY_CLIPBOARD;
        return rejected;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    if (!slotPayloadValid(payload)) {
        rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return rejected;
    }
    const auto* target = core::state::macro::macroAutomationFindSlot(
        pages.automation,
        address
    );
    const uint16_t required = payload.sourceSlotPresent
        ? core::state::macro::macroAutomationStoredPointCount(
              payload.entries[0].state,
              payload.pointPool
          )
        : 0;
    const uint16_t reclaimable = target != nullptr
        ? core::state::macro::macroAutomationStoredPointCount(
              *target,
              pages.automation.pointPool
          )
        : 0;
    const auto& targetPage = pages.pageData(address.track, address.page);
    const bool populated = targetPage.isMacroActive(address.macro) || target != nullptr;
    return preflightCapacity(
        pages,
        address,
        required,
        reclaimable,
        payload.sourceSlotPresent,
        populated
    );
}

FLASHMEM bool pasteSlotFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
) {
    const auto plan = preflightSlotPaste(pages, address, clipboard);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    if (payload.sourceSlotPresent) {
        auto* target = core::state::macro::macroAutomationGetOrCreateSlot(
            pages.automation,
            address
        );
        if (target == nullptr || !core::state::macro::macroAutomationCopySlotState(
                pages.automation,
                *target,
                payload.pointPool,
                payload.entries[0].state
            )) {
            return false;
        }
    } else {
        (void)core::state::macro::macroAutomationClearSlot(
            pages.automation,
            address
        );
    }

    auto& page = pages.pageData(address.track, address.page);
    page.setMacroActive(address.macro, payload.sourceMacroActive);
    page.cc[address.macro] = payload.sourceCc;
    page.values[address.macro] = core::state::macro::macroAutomationClamp01(
        payload.sourceStaticValue
    );
    if (pages.currentActiveTrack() == address.track &&
        pages.currentActivePage() == address.page) {
        pages.updateActiveConfigs();
    }
    return true;
}

FLASHMEM bool copyModulationToClipboard(
    const core::state::macro::MacroAutomationBankState& bank,
    const core::state::macro::MacroAutomationSlotAddress& address,
    core::state::StructureClipboardState& clipboard
) {
    return clipboard.storeMacroModulation(bank, address);
}

FLASHMEM MacroTypedPastePreflight preflightModulationPaste(
    const core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard
) {
    MacroTypedPastePreflight rejected{};
    if (!core::state::macro::macroAutomationAddressValid(address) ||
        !pages.pageData(address.track, address.page).isMacroActive(address.macro)) {
        rejected.status = MacroTypedPasteStatus::INVALID_TARGET;
        return rejected;
    }
    if (!clipboard.hasMacroModulation() || !clipboard.macroAutomationSet) {
        rejected.status = MacroTypedPasteStatus::EMPTY_CLIPBOARD;
        return rejected;
    }
    const auto& payload = *clipboard.macroAutomationSet;
    if (!slotPayloadValid(payload) ||
        !core::state::macro::macroCurveStored(payload.entries[0].state.modulation)) {
        rejected.status = MacroTypedPasteStatus::INVALID_PAYLOAD;
        return rejected;
    }
    const auto* target = core::state::macro::macroAutomationFindSlot(
        pages.automation,
        address
    );
    const uint16_t required = payload.entries[0].state.modulation.pointCount;
    const uint16_t reclaimable = target != nullptr
        ? core::state::macro::macroAutomationStoredPointCount(
              target->modulation,
              pages.automation.pointPool
          )
        : 0;
    const bool populated = target != nullptr &&
        core::state::macro::macroCurveStored(target->modulation);
    return preflightCapacity(
        pages,
        address,
        required,
        reclaimable,
        true,
        populated
    );
}

FLASHMEM bool pasteModulationFromClipboard(
    core::state::macro::MacroPagesState& pages,
    const core::state::macro::MacroAutomationSlotAddress& address,
    const core::state::StructureClipboardState& clipboard,
    bool overwriteConfirmed
) {
    const auto plan = preflightModulationPaste(pages, address, clipboard);
    if (!plan.actionable() || (plan.requiresOverwrite() && !overwriteConfirmed)) {
        return false;
    }
    auto* target = core::state::macro::macroAutomationGetOrCreateSlot(
        pages.automation,
        address
    );
    if (target == nullptr) return false;
    const auto& payload = *clipboard.macroAutomationSet;
    return core::state::macro::macroAutomationCopyModulationState(
        pages.automation,
        *target,
        payload.pointPool,
        payload.entries[0].state
    );
}

}  // namespace core::handler::macro::automation_clipboard_ops

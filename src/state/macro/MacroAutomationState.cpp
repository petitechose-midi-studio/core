#include "state/macro/MacroAutomationState.hpp"

#include <config/PlatformCompat.hpp>

namespace core::state::macro {

FLASHMEM void MacroAutomationBankState::clear() {
    entryCount = 0;
    entries = {};
}

FLASHMEM bool macroAutomationAddressValid(const MacroAutomationSlotAddress& address) {
    return address.track < TRACK_COUNT &&
           address.page < PAGE_COUNT &&
           address.macro < MACRO_COUNT;
}

FLASHMEM bool macroAutomationAddressEquals(const MacroAutomationSlotAddress& lhs,
                                           const MacroAutomationSlotAddress& rhs) {
    return lhs.track == rhs.track &&
           lhs.page == rhs.page &&
           lhs.macro == rhs.macro;
}

FLASHMEM const MacroAutomationSlotState* macroAutomationFindSlot(
    const MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
) {
    if (!macroAutomationAddressValid(address)) return nullptr;
    const uint8_t count = bank.entryCount > MACRO_AUTOMATION_SLOT_CAPACITY
        ? MACRO_AUTOMATION_SLOT_CAPACITY
        : bank.entryCount;
    for (uint8_t i = 0; i < count; ++i) {
        const auto& entry = bank.entries[i];
        if (entry.active && macroAutomationAddressEquals(entry.address, address)) {
            return &entry.state;
        }
    }
    return nullptr;
}

FLASHMEM MacroAutomationSlotState* macroAutomationFindMutableSlot(
    MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
) {
    return const_cast<MacroAutomationSlotState*>(
        macroAutomationFindSlot(static_cast<const MacroAutomationBankState&>(bank), address)
    );
}

FLASHMEM MacroAutomationSlotState* macroAutomationGetOrCreateSlot(
    MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
) {
    if (!macroAutomationAddressValid(address)) return nullptr;
    if (auto* existing = macroAutomationFindMutableSlot(bank, address)) {
        return existing;
    }
    if (bank.entryCount >= MACRO_AUTOMATION_SLOT_CAPACITY) return nullptr;
    const uint8_t index = bank.entryCount;
    bank.entries[index] = MacroAutomationSlotEntry{
        .active = true,
        .address = address,
        .state = {},
    };
    bank.entryCount = static_cast<uint8_t>(bank.entryCount + 1U);
    return &bank.entries[index].state;
}

FLASHMEM bool macroAutomationClearSlot(MacroAutomationBankState& bank,
                                       const MacroAutomationSlotAddress& address) {
    if (!macroAutomationAddressValid(address)) return false;
    const uint8_t count = bank.entryCount > MACRO_AUTOMATION_SLOT_CAPACITY
        ? MACRO_AUTOMATION_SLOT_CAPACITY
        : bank.entryCount;
    for (uint8_t i = 0; i < count; ++i) {
        if (!bank.entries[i].active ||
            !macroAutomationAddressEquals(bank.entries[i].address, address)) {
            continue;
        }
        for (uint8_t j = i; j + 1U < count; ++j) {
            bank.entries[j] = bank.entries[j + 1U];
        }
        bank.entries[count - 1U] = {};
        bank.entryCount = static_cast<uint8_t>(count - 1U);
        return true;
    }
    return false;
}

}  // namespace core::state::macro

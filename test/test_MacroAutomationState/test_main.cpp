#ifdef NDEBUG
#undef NDEBUG
#endif

#include <cassert>
#include <iostream>

#include "../../src/state/macro/MacroAutomationState.hpp"

namespace {

namespace macro = core::state::macro;

void test_macro_automation_bank_creates_finds_and_clears_slots() {
    macro::MacroAutomationBankState bank;
    const macro::MacroAutomationSlotAddress address{.track = 1, .page = 2, .macro = 3};

    assert(macro::macroAutomationFindSlot(bank, address) == nullptr);
    auto* slot = macro::macroAutomationGetOrCreateSlot(bank, address);
    assert(slot != nullptr);
    assert(bank.entryCount == 1);

    slot->modulationDepth = 0.5f;
    assert(macro::macroAutomationFindSlot(bank, address)->modulationDepth == 0.5f);
    assert(macro::macroAutomationGetOrCreateSlot(bank, address) == slot);
    assert(bank.entryCount == 1);

    assert(macro::macroAutomationClearSlot(bank, address));
    assert(bank.entryCount == 0);
    assert(macro::macroAutomationFindSlot(bank, address) == nullptr);

    std::cout << "[PASS] test_macro_automation_bank_creates_finds_and_clears_slots\n";
}

void test_macro_automation_bank_rejects_invalid_or_full_slots() {
    macro::MacroAutomationBankState bank;
    assert(macro::macroAutomationGetOrCreateSlot(
               bank,
               macro::MacroAutomationSlotAddress{
                   .track = macro::TRACK_COUNT,
                   .page = 0,
                   .macro = 0,
               }) == nullptr);

    for (uint8_t i = 0; i < macro::MACRO_AUTOMATION_SLOT_CAPACITY; ++i) {
        auto* slot = macro::macroAutomationGetOrCreateSlot(
            bank,
            macro::MacroAutomationSlotAddress{
                .track = static_cast<uint8_t>(i / macro::PAGE_COUNT),
                .page = static_cast<uint8_t>(i % macro::PAGE_COUNT),
                .macro = 0,
            }
        );
        assert(slot != nullptr);
    }
    assert(bank.entryCount == macro::MACRO_AUTOMATION_SLOT_CAPACITY);
    assert(macro::macroAutomationGetOrCreateSlot(
               bank,
               macro::MacroAutomationSlotAddress{.track = 15, .page = 15, .macro = 7}) ==
           nullptr);

    std::cout << "[PASS] test_macro_automation_bank_rejects_invalid_or_full_slots\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "MacroAutomationState tests\n";
    std::cout << "==============================================\n\n";

    test_macro_automation_bank_creates_finds_and_clears_slots();
    test_macro_automation_bank_rejects_invalid_or_full_slots();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

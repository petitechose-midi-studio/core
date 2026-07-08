#pragma once

#include <array>
#include <cstdint>
#include <type_traits>

#include "state/macro/MacroAutomationDomain.hpp"
#include "state/macro/MacroConstants.hpp"

namespace core::state::macro {

static constexpr uint8_t MACRO_AUTOMATION_SLOT_CAPACITY = 64;

struct MacroAutomationSlotAddress {
    uint8_t track = 0;
    uint8_t page = 0;
    uint8_t macro = 0;
};

struct MacroAutomationSlotEntry {
    bool active = false;
    MacroAutomationSlotAddress address{};
    MacroAutomationSlotState state{};
};

struct MacroAutomationBankState {
    uint8_t entryCount = 0;
    std::array<MacroAutomationSlotEntry, MACRO_AUTOMATION_SLOT_CAPACITY> entries{};
    MacroAutomationPointPool pointPool{};

    void clear();
};

static_assert(std::is_trivially_copyable_v<MacroAutomationSlotAddress>);
static_assert(std::is_trivially_copyable_v<MacroAutomationSlotEntry>);
static_assert(std::is_trivially_copyable_v<MacroAutomationPointPool>);
static_assert(std::is_trivially_copyable_v<MacroAutomationBankState>);

bool macroAutomationAddressValid(const MacroAutomationSlotAddress& address);
bool macroAutomationAddressEquals(const MacroAutomationSlotAddress& lhs,
                                  const MacroAutomationSlotAddress& rhs);

const MacroAutomationSlotState* macroAutomationFindSlot(
    const MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
);

MacroAutomationSlotState* macroAutomationFindMutableSlot(
    MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
);

MacroAutomationSlotState* macroAutomationGetOrCreateSlot(
    MacroAutomationBankState& bank,
    const MacroAutomationSlotAddress& address
);

bool macroAutomationClearSlot(MacroAutomationBankState& bank,
                              const MacroAutomationSlotAddress& address);
bool macroAutomationClearPage(MacroAutomationBankState& bank,
                              uint8_t track,
                              uint8_t page);
bool macroAutomationClearTrack(MacroAutomationBankState& bank,
                               uint8_t track);

bool macroAutomationSlotHasContent(const MacroAutomationSlotState& state);
void macroAutomationCompactPool(MacroAutomationBankState& bank);
// `slot` must be owned by `bank`; compaction rewrites pool offsets through bank entries.
bool macroAutomationAssignAutomation(MacroAutomationBankState& bank,
                                     MacroAutomationSlotState& slot,
                                     const MacroAutomationLane& lane);
// `slot` must be owned by `bank`; compaction rewrites pool offsets through bank entries.
bool macroAutomationAssignModulation(MacroAutomationBankState& bank,
                                     MacroAutomationSlotState& slot,
                                     const MacroModulationShape& shape);
void macroAutomationClearAutomation(MacroAutomationBankState& bank,
                                    MacroAutomationSlotState& slot);
void macroAutomationClearModulation(MacroAutomationBankState& bank,
                                    MacroAutomationSlotState& slot);
bool macroAutomationCopySlotState(MacroAutomationPointPool& destPool,
                                  MacroAutomationSlotState& dest,
                                  const MacroAutomationPointPool& sourcePool,
                                  const MacroAutomationSlotState& source);
bool macroAutomationCopySlotState(MacroAutomationBankState& destBank,
                                  MacroAutomationSlotState& dest,
                                  const MacroAutomationPointPool& sourcePool,
                                  const MacroAutomationSlotState& source);

}  // namespace core::state::macro

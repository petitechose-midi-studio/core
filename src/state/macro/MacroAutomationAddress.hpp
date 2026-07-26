#pragma once

#include <cstdint>
#include <type_traits>

#include "state/macro/MacroConstants.hpp"

namespace core::state::macro {

/**
 * Stable logical address of one physical Macro destination.
 *
 * This value identifies Project Control graph destinations; it owns no
 * automation or modulation storage.
 */
struct MacroAutomationSlotAddress {
    uint8_t track = 0;
    uint8_t page = 0;
    uint8_t macro = 0;
};

[[nodiscard]] constexpr bool macroAutomationAddressValid(
    const MacroAutomationSlotAddress& address
) {
    return address.track < TRACK_COUNT &&
           address.page < PAGE_COUNT &&
           address.macro < MACRO_COUNT;
}

[[nodiscard]] constexpr bool macroAutomationAddressEquals(
    const MacroAutomationSlotAddress& lhs,
    const MacroAutomationSlotAddress& rhs
) {
    return lhs.track == rhs.track &&
           lhs.page == rhs.page &&
           lhs.macro == rhs.macro;
}

static_assert(std::is_trivially_copyable_v<MacroAutomationSlotAddress>);
static_assert(sizeof(MacroAutomationSlotAddress) == 3U);

}  // namespace core::state::macro

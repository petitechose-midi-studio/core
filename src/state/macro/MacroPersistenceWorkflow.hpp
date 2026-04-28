#pragma once

#include <cstdint>

#include "persistence/PersistenceSlotFileStore.hpp"

namespace core::state {
struct CoreState;
}

namespace core::state::macro {

/**
 * CoreState-facing macro library persistence workflow.
 *
 * Saves first sync runtime macro values back into the active page, and loads
 * refresh shared track state, runtime projection, status label, and config
 * revision.
 */
struct MacroPersistenceWorkflow {
    static bool saveLibrarySlot(CoreState& state, uint8_t slotIndex);
    static persistence::SlotLoadStatus loadLibrarySlot(CoreState& state, uint8_t slotIndex);
    static bool eraseLibrarySlot(CoreState& state, uint8_t slotIndex);
};

}  // namespace core::state::macro

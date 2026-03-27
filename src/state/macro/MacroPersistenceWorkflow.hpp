#pragma once

#include <cstdint>

#include "persistence/PersistenceSlotFileStore.hpp"

namespace core::state {
struct CoreState;
}

namespace core::state::macro {

struct MacroPersistenceWorkflow {
    static bool saveLibrarySlot(CoreState& state, uint8_t slotIndex);
    static persistence::SlotLoadStatus loadLibrarySlot(CoreState& state, uint8_t slotIndex);
    static bool eraseLibrarySlot(CoreState& state, uint8_t slotIndex);
};

}  // namespace core::state::macro

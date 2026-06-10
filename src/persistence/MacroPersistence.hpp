#pragma once

#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceSlotFileStore.hpp"
#include "state/macro/MacroPagesState.hpp"

namespace core::persistence {

/**
 * Persists explicit macro library slots.
 *
 * Library slots store the full macro track bank plus shared track state so
 * loading a slot can restore the selected shared track context.
 */
class MacroPersistence {
public:
    static constexpr uint16_t LIBRARY_SLOT_COUNT = 16;

    static constexpr uint32_t LIBRARY_MAGIC = 0x4D4C4942;    // "MLIB"
    static constexpr uint8_t LIBRARY_DATA_VERSION = 1;

    explicit MacroPersistence(oc::interface::IStorage& libraryStorage);

    bool init();
    PersistenceWriteStatus initStatus();

    bool saveLibrarySlot(uint8_t slotIndex, const state::macro::MacroPagesState& pages);
    PersistenceWriteStatus saveLibrarySlotStatus(uint8_t slotIndex,
                                                 const state::macro::MacroPagesState& pages);
    SlotLoadStatus loadLibrarySlot(uint8_t slotIndex, state::macro::MacroPagesState& pages);
    bool eraseLibrarySlot(uint8_t slotIndex);
    PersistenceWriteStatus eraseLibrarySlotStatus(uint8_t slotIndex);

private:
    PersistenceSlotFileStore library_store_;
};

}  // namespace core::persistence

#pragma once

#include <cstdint>

#include <oc/interface/IStorage.hpp>

#include "persistence/PersistenceSlotFileStore.hpp"
#include "state/macro/MacroPagesState.hpp"

namespace core::persistence {

/**
 * Persists macro workspace and library slots.
 *
 * Workspace saves rotate across two slots for latest-valid recovery. Library
 * slots store the full macro track bank plus shared track state so loading a
 * slot can restore the selected shared track context.
 */
class MacroPersistence {
public:
    static constexpr uint16_t WORKSPACE_SLOT_COUNT = 2;
    static constexpr uint16_t LIBRARY_SLOT_COUNT = 16;

    static constexpr uint32_t WORKSPACE_MAGIC = 0x4D57534B;  // "MWSK"
    static constexpr uint32_t LIBRARY_MAGIC = 0x4D4C4942;    // "MLIB"
    static constexpr uint8_t WORKSPACE_DATA_VERSION = 2;
    static constexpr uint8_t LIBRARY_DATA_VERSION = 1;

    explicit MacroPersistence(oc::interface::IStorage& workspaceStorage,
                              oc::interface::IStorage& libraryStorage);

    bool init();
    PersistenceWriteStatus initStatus();

    bool loadWorkspace(state::macro::MacroPagesState& pages);
    bool saveWorkspace(const state::macro::MacroPagesState& pages);
    PersistenceWriteStatus saveWorkspaceStatus(const state::macro::MacroPagesState& pages);

    bool saveLibrarySlot(uint8_t slotIndex, const state::macro::MacroPagesState& pages);
    PersistenceWriteStatus saveLibrarySlotStatus(uint8_t slotIndex,
                                                 const state::macro::MacroPagesState& pages);
    SlotLoadStatus loadLibrarySlot(uint8_t slotIndex, state::macro::MacroPagesState& pages);
    bool eraseLibrarySlot(uint8_t slotIndex);
    PersistenceWriteStatus eraseLibrarySlotStatus(uint8_t slotIndex);

private:
    PersistenceSlotFileStore workspace_store_;
    PersistenceSlotFileStore library_store_;
    uint32_t next_workspace_counter_ = 1;
    uint16_t next_workspace_slot_ = 0;
};

}  // namespace core::persistence

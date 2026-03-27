#pragma once

#include <cstdint>

#include "DataManagerCatalog.hpp"
#include "persistence/PersistenceSlotFileStore.hpp"

namespace core::state {

struct CoreState;

struct DataManagerCommandExecutionResult {
    bool handled = false;
    bool success = false;
    bool isLoadOperation = false;
    bool deferredApply = false;
    persistence::SlotLoadStatus loadStatus = persistence::SlotLoadStatus::OK;
};

struct DataManagerWorkflow {
    static uint8_t slotCount(DataManagerCommand command);
    static bool slotOccupied(CoreState& state, DataManagerCommand command, uint8_t slotIndex);
    static DataManagerCommandExecutionResult execute(
        CoreState& state,
        DataManagerCommand command,
        uint8_t slotIndex,
        DataManagerSetLoadMode setLoadMode
    );
    static void setShortcut(
        CoreState& state,
        DataManagerContext context,
        bool leftButton,
        DataManagerCommand command
    );
    static void loadShortcutsFromSettings(CoreState& state);
};

}  // namespace core::state

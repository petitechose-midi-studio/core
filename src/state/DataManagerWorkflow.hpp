#pragma once

#include <cstdint>

#include "CoreSettings.hpp"
#include "DataManagerCatalog.hpp"
#include "DataManagerState.hpp"
#include "persistence/PersistenceSlotFileStore.hpp"

namespace core::state {

struct DataManagerCommandExecutionResult {
    bool handled = false;
    bool success = false;
    bool isLoadOperation = false;
    bool deferredApply = false;
    persistence::SlotLoadStatus loadStatus = persistence::SlotLoadStatus::OK;
};

/**
 * UI-level workflow for Data Manager.
 *
 * The operations keep command selection and shortcut persistence testable
 * without CoreState. Production bridges provide slot probing and command
 * execution from composition code.
 */
struct DataManagerWorkflow {
    using SlotOccupiedFn = bool (*)(void* context, DataManagerCommand command, uint8_t slotIndex);
    using ExecuteFn = DataManagerCommandExecutionResult (*)(
        void* context,
        DataManagerCommand command,
        uint8_t slotIndex,
        DataManagerSetLoadMode setLoadMode
    );

    struct StateRefs {
        DataManagerState& dataManager;
        CoreSettings& settings;
    };

    struct Operations {
        void* context = nullptr;
        SlotOccupiedFn slotOccupiedFn = nullptr;
        ExecuteFn executeFn = nullptr;

        bool slotOccupied(DataManagerCommand command, uint8_t slotIndex) const;
        DataManagerCommandExecutionResult execute(DataManagerCommand command,
                                                  uint8_t slotIndex,
                                                  DataManagerSetLoadMode setLoadMode) const;
    };

    static uint8_t slotCount(DataManagerCommand command);
    static bool slotOccupied(
        StateRefs state,
        Operations operations,
        DataManagerCommand command,
        uint8_t slotIndex
    );
    static DataManagerCommandExecutionResult execute(
        StateRefs state,
        Operations operations,
        DataManagerCommand command,
        uint8_t slotIndex,
        DataManagerSetLoadMode setLoadMode
    );
    static void setShortcut(
        StateRefs state,
        DataManagerContext context,
        bool leftButton,
        DataManagerCommand command
    );
    static void loadShortcutsFromSettings(StateRefs state);
};

}  // namespace core::state

#pragma once

#include <cstdint>

#include "CoreSettings.hpp"
#include "DataManagerCatalog.hpp"
#include "DataManagerState.hpp"
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

/**
 * UI-level workflow for Data Manager.
 *
 * The hookable overloads keep command selection and shortcut persistence
 * testable without CoreState. CoreState overloads provide the production bridge
 * to slot probing and command execution.
 */
struct DataManagerWorkflow {
    struct StateRefs {
        DataManagerState& dataManager;
        CoreSettings& settings;
    };

    struct Hooks {
        CoreState* coreState = nullptr;

        bool slotOccupied(DataManagerCommand command, uint8_t slotIndex) const;
        DataManagerCommandExecutionResult execute(DataManagerCommand command,
                                                  uint8_t slotIndex,
                                                  DataManagerSetLoadMode setLoadMode) const;
    };

    static uint8_t slotCount(DataManagerCommand command);
    static bool slotOccupied(StateRefs state, Hooks hooks, DataManagerCommand command, uint8_t slotIndex);
    static bool slotOccupied(CoreState& state, DataManagerCommand command, uint8_t slotIndex);
    static DataManagerCommandExecutionResult execute(
        StateRefs state,
        Hooks hooks,
        DataManagerCommand command,
        uint8_t slotIndex,
        DataManagerSetLoadMode setLoadMode
    );
    static DataManagerCommandExecutionResult execute(
        CoreState& state,
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
    static void setShortcut(
        CoreState& state,
        DataManagerContext context,
        bool leftButton,
        DataManagerCommand command
    );
    static void loadShortcutsFromSettings(StateRefs state);
    static void loadShortcutsFromSettings(CoreState& state);
};

}  // namespace core::state

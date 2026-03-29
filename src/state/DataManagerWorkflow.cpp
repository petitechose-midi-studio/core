#include "state/DataManagerWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include "state/DataManagerCommandExecutor.hpp"
#include "state/DataManagerShortcutPersistence.hpp"

namespace core::state {

FLASHMEM uint8_t DataManagerWorkflow::slotCount(DataManagerCommand command) {
    return data_manager::slotCount(command);
}

FLASHMEM bool DataManagerWorkflow::slotOccupied(CoreState& state,
                                                DataManagerCommand command,
                                                uint8_t slotIndex) {
    return data_manager::slotOccupied(state, command, slotIndex);
}

FLASHMEM DataManagerCommandExecutionResult DataManagerWorkflow::execute(
    CoreState& state,
    DataManagerCommand command,
    uint8_t slotIndex,
    DataManagerSetLoadMode setLoadMode
) {
    return data_manager::execute(state, command, slotIndex, setLoadMode);
}

FLASHMEM void DataManagerWorkflow::setShortcut(CoreState& state,
                                               DataManagerContext context,
                                               bool leftButton,
                                               DataManagerCommand command) {
    data_manager::setShortcut(state, context, leftButton, command);
}

FLASHMEM void DataManagerWorkflow::loadShortcutsFromSettings(CoreState& state) {
    data_manager::loadShortcutsFromSettings(state);
}

}  // namespace core::state

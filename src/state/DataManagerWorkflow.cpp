#include "state/DataManagerWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include "persistence/MacroPersistence.hpp"
#include "persistence/SequencerPersistence.hpp"
#include "state/DataManagerShortcutPersistence.hpp"

namespace core::state {

FLASHMEM bool DataManagerWorkflow::Operations::slotOccupied(DataManagerCommand command,
                                                            uint8_t slotIndex) const {
    return slotOccupiedFn != nullptr && slotOccupiedFn(context, command, slotIndex);
}

FLASHMEM DataManagerCommandExecutionResult DataManagerWorkflow::Operations::execute(
    DataManagerCommand command,
    uint8_t slotIndex,
    DataManagerSetLoadMode setLoadMode
) const {
    if (executeFn == nullptr) {
        return {};
    }
    return executeFn(context, command, slotIndex, setLoadMode);
}

FLASHMEM uint8_t DataManagerWorkflow::slotCount(DataManagerCommand command) {
    switch (dataManagerSlotDomain(command)) {
        case DataManagerSlotDomain::MACRO_LIBRARY:
            return persistence::MacroPersistence::LIBRARY_SLOT_COUNT;
        case DataManagerSlotDomain::SEQ_PATTERN_LIBRARY:
            return persistence::SequencerPersistence::PATTERN_LIBRARY_SLOT_COUNT;
        case DataManagerSlotDomain::SEQ_SET_LIBRARY:
            return persistence::SequencerPersistence::SET_LIBRARY_SLOT_COUNT;
        case DataManagerSlotDomain::NONE:
        default:
            return 0;
    }
}

FLASHMEM bool DataManagerWorkflow::slotOccupied(StateRefs,
                                                Operations operations,
                                                DataManagerCommand command,
                                                uint8_t slotIndex) {
    return operations.slotOccupied(command, slotIndex);
}

FLASHMEM DataManagerCommandExecutionResult DataManagerWorkflow::execute(
    StateRefs,
    Operations operations,
    DataManagerCommand command,
    uint8_t slotIndex,
    DataManagerSetLoadMode setLoadMode
) {
    return operations.execute(command, slotIndex, setLoadMode);
}

FLASHMEM void DataManagerWorkflow::setShortcut(StateRefs state,
                                               DataManagerContext context,
                                               bool leftButton,
                                               DataManagerCommand command) {
    data_manager::setShortcut(
        data_manager::ShortcutStateRefs{
            state.dataManager,
            state.settings,
        },
        context,
        leftButton,
        command
    );
}

FLASHMEM void DataManagerWorkflow::loadShortcutsFromSettings(StateRefs state) {
    data_manager::loadShortcutsFromSettings(data_manager::ShortcutStateRefs{
        state.dataManager,
        state.settings,
    });
}

}  // namespace core::state

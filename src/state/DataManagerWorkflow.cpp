#include "state/DataManagerWorkflow.hpp"

#include <config/PlatformCompat.hpp>
#include "state/CoreState.hpp"
#include "state/DataManagerCommandExecutor.hpp"
#include "state/DataManagerShortcutPersistence.hpp"

namespace core::state {

namespace {

DataManagerWorkflow::StateRefs makeStateRefs(CoreState& state) {
    return DataManagerWorkflow::StateRefs{
        state.dataManager,
        state.settings,
    };
}

DataManagerWorkflow::Hooks makeHooks(CoreState& state) {
    return DataManagerWorkflow::Hooks{&state};
}

}  // namespace

FLASHMEM bool DataManagerWorkflow::Hooks::slotOccupied(DataManagerCommand command,
                                                       uint8_t slotIndex) const {
    return coreState ? data_manager::slotOccupied(*coreState, command, slotIndex) : false;
}

FLASHMEM DataManagerCommandExecutionResult DataManagerWorkflow::Hooks::execute(
    DataManagerCommand command,
    uint8_t slotIndex,
    DataManagerSetLoadMode setLoadMode
) const {
    if (!coreState) {
        return {};
    }
    return data_manager::execute(*coreState, command, slotIndex, setLoadMode);
}

FLASHMEM uint8_t DataManagerWorkflow::slotCount(DataManagerCommand command) {
    return data_manager::slotCount(command);
}

FLASHMEM bool DataManagerWorkflow::slotOccupied(StateRefs,
                                                Hooks hooks,
                                                DataManagerCommand command,
                                                uint8_t slotIndex) {
    return hooks.slotOccupied(command, slotIndex);
}

FLASHMEM bool DataManagerWorkflow::slotOccupied(CoreState& state,
                                                DataManagerCommand command,
                                                uint8_t slotIndex) {
    return slotOccupied(makeStateRefs(state), makeHooks(state), command, slotIndex);
}

FLASHMEM DataManagerCommandExecutionResult DataManagerWorkflow::execute(
    StateRefs,
    Hooks hooks,
    DataManagerCommand command,
    uint8_t slotIndex,
    DataManagerSetLoadMode setLoadMode
) {
    return hooks.execute(command, slotIndex, setLoadMode);
}

FLASHMEM DataManagerCommandExecutionResult DataManagerWorkflow::execute(
    CoreState& state,
    DataManagerCommand command,
    uint8_t slotIndex,
    DataManagerSetLoadMode setLoadMode
) {
    return execute(makeStateRefs(state), makeHooks(state), command, slotIndex, setLoadMode);
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

FLASHMEM void DataManagerWorkflow::setShortcut(CoreState& state,
                                               DataManagerContext context,
                                               bool leftButton,
                                               DataManagerCommand command) {
    setShortcut(makeStateRefs(state), context, leftButton, command);
}

FLASHMEM void DataManagerWorkflow::loadShortcutsFromSettings(StateRefs state) {
    data_manager::loadShortcutsFromSettings(data_manager::ShortcutStateRefs{
        state.dataManager,
        state.settings,
    });
}

FLASHMEM void DataManagerWorkflow::loadShortcutsFromSettings(CoreState& state) {
    loadShortcutsFromSettings(makeStateRefs(state));
}

}  // namespace core::state

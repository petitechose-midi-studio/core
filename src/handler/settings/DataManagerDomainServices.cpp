#include "handler/settings/DataManagerDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"

namespace core::handler {

FLASHMEM DataManagerDomainServices::DataManagerDomainServices(StateRefs state, Hooks hooks)
    : data_manager_(&state.dataManager)
    , settings_(&state.settings)
    , hooks_(hooks) {}

FLASHMEM DataManagerDomainServices DataManagerDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return DataManagerDomainServices{
        StateRefs{
            state.dataManager,
            state.settings,
        },
        Hooks{&state},
    };
}

FLASHMEM uint8_t DataManagerDomainServices::slotCount(core::state::DataManagerCommand command) const {
    return core::state::DataManagerWorkflow::slotCount(command);
}

FLASHMEM bool DataManagerDomainServices::slotOccupied(core::state::DataManagerCommand command,
                                                      uint8_t slotIndex) const {
    return core::state::DataManagerWorkflow::slotOccupied(
        core::state::DataManagerWorkflow::StateRefs{
            *data_manager_,
            *settings_,
        },
        hooks_,
        command,
        slotIndex
    );
}

FLASHMEM core::state::DataManagerCommandExecutionResult DataManagerDomainServices::execute(
    core::state::DataManagerCommand command,
    uint8_t slotIndex,
    core::state::DataManagerSetLoadMode setLoadMode
) const {
    return core::state::DataManagerWorkflow::execute(
        core::state::DataManagerWorkflow::StateRefs{
            *data_manager_,
            *settings_,
        },
        hooks_,
        command,
        slotIndex,
        setLoadMode
    );
}

FLASHMEM void DataManagerDomainServices::setShortcut(core::state::DataManagerContext context,
                                                     bool leftButton,
                                                     core::state::DataManagerCommand command) const {
    core::state::DataManagerWorkflow::setShortcut(
        core::state::DataManagerWorkflow::StateRefs{
            *data_manager_,
            *settings_,
        },
        context,
        leftButton,
        command
    );
}

}  // namespace core::handler

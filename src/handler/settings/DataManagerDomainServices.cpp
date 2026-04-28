#include "handler/settings/DataManagerDomainServices.hpp"

#include <config/PlatformCompat.hpp>

#include "state/CoreState.hpp"
#include "state/DataManagerCommandExecutor.hpp"

namespace core::handler {

namespace {

FLASHMEM bool slotOccupiedFromCoreState(
    void* context,
    core::state::DataManagerCommand command,
    uint8_t slotIndex
) {
    auto* state = static_cast<core::state::CoreState*>(context);
    return state != nullptr && core::state::data_manager::slotOccupied(
        *state,
        command,
        slotIndex
    );
}

FLASHMEM core::state::DataManagerCommandExecutionResult executeFromCoreState(
    void* context,
    core::state::DataManagerCommand command,
    uint8_t slotIndex,
    core::state::DataManagerSetLoadMode setLoadMode
) {
    auto* state = static_cast<core::state::CoreState*>(context);
    if (state == nullptr) {
        return {};
    }
    return core::state::data_manager::execute(*state, command, slotIndex, setLoadMode);
}

}  // namespace

FLASHMEM DataManagerDomainServices::DataManagerDomainServices(
    StateRefs state,
    Operations operations
)
    : data_manager_(&state.dataManager)
    , settings_(&state.settings)
    , operations_(operations) {}

FLASHMEM DataManagerDomainServices DataManagerDomainServices::fromCoreState(
    core::state::CoreState& state
) {
    return DataManagerDomainServices{
        StateRefs{
            state.dataManager,
            state.settings,
        },
        Operations{&state, slotOccupiedFromCoreState, executeFromCoreState},
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
        operations_,
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
        operations_,
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

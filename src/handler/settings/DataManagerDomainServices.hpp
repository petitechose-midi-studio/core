#pragma once

#include <cstdint>

#include "state/DataManagerWorkflow.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

/**
 * Handler-facing facade over DataManagerWorkflow.
 *
 * Slot probing, command execution, and shortcut persistence are routed through
 * workflow operations so DataManagerHandler stays focused on modal input flow.
 */
class DataManagerDomainServices {
public:
    using StateRefs = core::state::DataManagerWorkflow::StateRefs;
    using Operations = core::state::DataManagerWorkflow::Operations;

    DataManagerDomainServices(StateRefs state, Operations operations);
    static DataManagerDomainServices fromCoreState(core::state::CoreState& state);

    uint8_t slotCount(core::state::DataManagerCommand command) const;
    bool slotOccupied(core::state::DataManagerCommand command, uint8_t slotIndex) const;
    core::state::DataManagerCommandExecutionResult execute(
        core::state::DataManagerCommand command,
        uint8_t slotIndex,
        core::state::DataManagerSetLoadMode setLoadMode
    ) const;
    void setShortcut(core::state::DataManagerContext context,
                     bool leftButton,
                     core::state::DataManagerCommand command) const;

private:
    core::state::DataManagerState* data_manager_ = nullptr;
    core::state::CoreSettings* settings_ = nullptr;
    Operations operations_{};
};

}  // namespace core::handler

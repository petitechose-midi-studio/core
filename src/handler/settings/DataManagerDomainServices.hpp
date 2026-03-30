#pragma once

#include <cstdint>

#include "state/DataManagerWorkflow.hpp"

namespace core::state {
struct CoreState;
}

namespace core::handler {

class DataManagerDomainServices {
public:
    using StateRefs = core::state::DataManagerWorkflow::StateRefs;
    using Hooks = core::state::DataManagerWorkflow::Hooks;

    DataManagerDomainServices(StateRefs state, Hooks hooks);
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
    Hooks hooks_{};
};

}  // namespace core::handler

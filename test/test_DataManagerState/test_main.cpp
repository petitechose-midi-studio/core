#include <cassert>
#include <cstring>

#include "../../src/state/DataManagerState.hpp"

int main() {
    core::state::DataManagerState state;

    assert(state.flowPhase.get() == core::state::DataManagerFlowPhase::CLOSED);
    assert(!state.visible.get());
    assert(!state.dialog.visible.get());

    state.openSession(core::state::DataManagerContext::SEQUENCER);
    assert(state.visible.get());
    assert(state.context.get() == core::state::DataManagerContext::SEQUENCER);
    assert(state.flowPhase.get() == core::state::DataManagerFlowPhase::MANAGER);

    state.pendingCommand.set(core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT);
    state.pendingSetLoadMode.set(core::state::DataManagerSetLoadMode::MERGE);
    state.showDialog(core::state::DataManagerDialogMode::SET_LOAD_MODE, 1);
    assert(state.dialog.visible.get());
    assert(state.dialog.mode.get() == core::state::DataManagerDialogMode::SET_LOAD_MODE);
    assert(state.flowPhase.get() == core::state::DataManagerFlowPhase::SET_LOAD_MODE);

    state.closeDialog();
    assert(!state.dialog.visible.get());
    assert(state.flowPhase.get() == core::state::DataManagerFlowPhase::MANAGER);

    state.clearPendingCommand();
    assert(state.pendingCommand.get() == core::state::DataManagerCommand::NONE);
    assert(state.pendingSetLoadMode.get() == core::state::DataManagerSetLoadMode::REPLACE);

    state.feedback.set("done");
    state.closeSession();
    assert(!state.visible.get());
    assert(state.flowPhase.get() == core::state::DataManagerFlowPhase::CLOSED);
    assert(!state.dialog.visible.get());
    assert(std::strcmp(state.feedback.get(), "") == 0);

    return 0;
}

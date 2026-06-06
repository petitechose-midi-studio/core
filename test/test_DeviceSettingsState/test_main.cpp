#include <cassert>

#include "../../src/state/DeviceSettingsState.hpp"

int main() {
    core::state::DeviceSettingsState state;

    assert(state.flowPhase.get() == core::state::DeviceSettingsFlowPhase::CLOSED);
    assert(!state.visible.get());
    assert(!state.selector.visible.get());

    state.openView();
    assert(state.visible.get());
    assert(state.flowPhase.get() == core::state::DeviceSettingsFlowPhase::VIEW);

    state.focusedRow.set(3);
    state.openSelector(3, 5);
    assert(state.selector.visible.get());
    assert(state.selector.editingRow.get() == 3);
    assert(state.selector.selectedIndex.get() == 5);
    assert(state.flowPhase.get() == core::state::DeviceSettingsFlowPhase::VALUE_SELECTOR);

    state.closeSelector();
    assert(!state.selector.visible.get());
    assert(state.flowPhase.get() == core::state::DeviceSettingsFlowPhase::VIEW);

    state.closeView();
    assert(!state.visible.get());
    assert(state.flowPhase.get() == core::state::DeviceSettingsFlowPhase::CLOSED);

    return 0;
}

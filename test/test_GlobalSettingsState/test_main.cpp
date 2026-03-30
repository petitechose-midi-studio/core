#include <cassert>

#include "../../src/state/GlobalSettingsState.hpp"

int main() {
    core::state::GlobalSettingsState state;

    assert(state.flowPhase.get() == core::state::GlobalSettingsFlowPhase::CLOSED);
    assert(!state.visible.get());
    assert(!state.selector.visible.get());

    state.openOverlay();
    assert(state.visible.get());
    assert(state.flowPhase.get() == core::state::GlobalSettingsFlowPhase::OVERLAY);

    state.focusedRow.set(3);
    state.openSelector(3, 5);
    assert(state.selector.visible.get());
    assert(state.selector.editingRow.get() == 3);
    assert(state.selector.selectedIndex.get() == 5);
    assert(state.flowPhase.get() == core::state::GlobalSettingsFlowPhase::VALUE_SELECTOR);

    state.closeSelector();
    assert(!state.selector.visible.get());
    assert(state.flowPhase.get() == core::state::GlobalSettingsFlowPhase::OVERLAY);

    state.closeOverlay();
    assert(!state.visible.get());
    assert(state.flowPhase.get() == core::state::GlobalSettingsFlowPhase::CLOSED);

    return 0;
}

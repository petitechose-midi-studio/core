#include <cassert>

#include "../../src/state/MacroEditState.hpp"

int main() {
    core::state::MacroEditState state;

    assert(state.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(!state.visible.get());
    assert(!state.selector.visible.get());
    assert(!state.macroSelector.visible.get());

    state.openEditor(2, 4, 74, 1000);
    assert(state.visible.get());
    assert(state.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);
    assert(state.editingIndex.get() == 2);
    assert(state.tempChannel.get() == 4);
    assert(state.tempCC.get() == 74);
    assert(state.pendingOpenReleaseDecision);

    state.openValueSelector(1, 31);
    assert(state.selector.visible.get());
    assert(state.selector.editingRow.get() == 1);
    assert(state.selector.selectedIndex.get() == 31);
    assert(state.flowPhase.get() == core::state::MacroEditFlowPhase::VALUE_SELECTOR);

    state.closeValueSelector();
    assert(!state.selector.visible.get());
    assert(state.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);

    state.openPageSelector();
    assert(state.flowPhase.get() == core::state::MacroEditFlowPhase::PAGE_SELECTOR);

    state.closePageSelector();
    assert(state.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);

    state.openTargetSelector(5);
    assert(state.macroSelector.visible.get());
    assert(state.macroSelector.selectedIndex.get() == 5);
    assert(state.flowPhase.get() == core::state::MacroEditFlowPhase::TARGET_SELECTOR);

    state.loadActiveConfig(5, 9, 12);
    assert(state.editingIndex.get() == 5);
    assert(state.tempChannel.get() == 9);
    assert(state.tempCC.get() == 12);

    state.closeTargetSelector();
    assert(!state.macroSelector.visible.get());
    assert(state.flowPhase.get() == core::state::MacroEditFlowPhase::EDIT);

    assert(!state.consumeOpeningReleaseDecision(1, 1600, 450));
    assert(state.pendingOpenReleaseDecision);

    assert(!state.consumeOpeningReleaseDecision(2, 1200, 450));
    assert(!state.pendingOpenReleaseDecision);

    state.openEditor(3, 0, 7, 2000);
    assert(state.consumeOpeningReleaseDecision(3, 2600, 450));
    assert(!state.pendingOpenReleaseDecision);

    state.closeEditor();
    assert(state.flowPhase.get() == core::state::MacroEditFlowPhase::CLOSED);
    assert(!state.visible.get());
    assert(!state.selector.visible.get());
    assert(!state.macroSelector.visible.get());

    return 0;
}

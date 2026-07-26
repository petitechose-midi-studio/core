#include <cassert>
#include <cstdint>
#include <iostream>

#include <oc/state/StaticSignalWatcher.hpp>

#include "../../src/state/StructureNavigationState.hpp"
#include "../../src/ui/common/StructureSelectionInvalidation.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

struct RenderProbe {
    uint8_t renderCount = 0U;

    void requestRender() {
        ++renderCount;
    }
};

void test_every_structure_selection_signal_invalidates_retained_projection() {
    core::state::StructureSelectionState selection;
    oc::state::StaticWatchGroup<
        core::ui::STRUCTURE_SELECTION_INVALIDATION_SIGNAL_COUNT>
        watcher;
    RenderProbe probe;

    watcher.bind<&RenderProbe::requestRender>(
        probe,
        0U,
        "test.structureSelection"
    );
    assert(core::ui::watchStructureSelectionInvalidation(watcher, selection));
    assert(
        watcher.subscriptionCount() ==
        core::ui::STRUCTURE_SELECTION_INVALIDATION_SIGNAL_COUNT
    );

    selection.active.set(true);
    test_support::drainNotifications();
    assert(probe.renderCount == 1U);

    selection.scope.set(core::state::StructureSelectionScope::TRACK);
    test_support::drainNotifications();
    assert(probe.renderCount == 2U);

    selection.cursorIndex.set(3U);
    test_support::drainNotifications();
    assert(probe.renderCount == 3U);

    selection.selectedMask.set(0x0009U);
    test_support::drainNotifications();
    assert(probe.renderCount == 4U);

    selection.active.set(false);
    selection.scope.set(core::state::StructureSelectionScope::PAGE);
    selection.cursorIndex.set(1U);
    selection.selectedMask.set(0x0002U);
    test_support::drainNotifications();
    assert(probe.renderCount == 5U);

    std::cout
        << "[PASS] test_every_structure_selection_signal_invalidates_retained_projection\n";
}

}  // namespace

int main() {
    test_every_structure_selection_signal_invalidates_retained_projection();
    test_support::drainNotifications();
    return 0;
}

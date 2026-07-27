#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"

namespace {
using test_support::CoreStorages;

void test_hide_all_invokes_cleanup_for_stacked_overlays() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings);

    std::vector<core::ui::OverlayType> cleaned;
    auto handle = state.overlays.setCleanupCallbackScoped(
        [&cleaned](core::ui::OverlayType type) { cleaned.push_back(type); }
    );

    state.overlays.show(core::ui::OverlayType::MACRO_EDIT, false);
    state.overlays.show(core::ui::OverlayType::MACRO_EDIT_SELECTOR, true);

    assert(state.macroEdit.visible.get());
    assert(state.macroEdit.selector.visible.get());

    state.overlays.hideAll();

    assert(state.overlays.current() == core::ui::OverlayType::NONE);
    assert(!state.macroEdit.visible.get());
    assert(!state.macroEdit.selector.visible.get());
    assert(cleaned.size() == 2);
    assert(cleaned[0] == core::ui::OverlayType::MACRO_EDIT);
    assert(cleaned[1] == core::ui::OverlayType::MACRO_EDIT_SELECTOR);

    std::cout << "[PASS] test_hide_all_invokes_cleanup_for_stacked_overlays\n";
}

}  // namespace

int main() {
    test_hide_all_invokes_cleanup_for_stacked_overlays();
    std::cout << "\nAll CoreState authority tests passed.\n";
    return 0;
}

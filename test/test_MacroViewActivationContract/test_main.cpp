#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "../../src/context/standalone/MacroViewActivationContract.hpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

using test_support::CoreStorages;
using test_support::drainNotifications;

void test_prepare_macro_view_activation_syncs_runtime_and_status_from_active_page() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    std::strncpy(state.pages.activeTrackData().pages[2].name,
                 "Mix Bus",
                 core::state::macro::PAGE_NAME_SIZE - 1);
    state.pages.activeTrackData().pages[2].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';
    state.pages.activeTrackData().pages[2].values[0] = 0.23f;
    state.pages.activeTrackData().pages[2].values[1] = 0.87f;
    state.pages.setActivePage(2);

    state.macros.slots[0].value.set(0.91f);
    state.macros.slots[1].value.set(0.11f);
    state.statusBar.pageName.set("Old Page");

    core::context::standalone::prepareMacroViewActivation(state);

    assert(state.pages.currentActivePage() == 2);
    assert(std::strcmp(state.statusBar.pageName.get(), "Mix Bus") == 0);
    assert(std::fabs(state.macros.slots[0].value.get() - 0.23f) < 0.0001f);
    assert(std::fabs(state.macros.slots[1].value.get() - 0.87f) < 0.0001f);
    assert(std::strcmp(state.macros.slots[0].label.get(), "Macro 1") == 0);

    drainNotifications();

    std::cout << "[PASS] test_prepare_macro_view_activation_syncs_runtime_and_status_from_active_page\n";
}

}  // namespace

int main() {
    test_prepare_macro_view_activation_syncs_runtime_and_status_from_active_page();
    std::cout << "\nAll macro view activation contract tests passed.\n";
    return 0;
}

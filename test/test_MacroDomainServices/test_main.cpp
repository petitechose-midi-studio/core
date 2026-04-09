#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/time/Time.hpp>

#include "../../src/handler/macro/MacroDomainServices.hpp"
// Native tests only build selected source folders; include the implementation
// here so this handler-level service remains testable without widening the
// environment's global src filter.
#include "../../src/handler/macro/MacroDomainServices.cpp"
#include "../../src/state/CoreState.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {

uint32_t g_mock_now_ms = 1000;

uint32_t mockTimeMs() {
    return g_mock_now_ms;
}

using test_support::CoreStorages;
using test_support::drainNotifications;

void test_runtime_values_are_forwarded_and_clamped() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroDomainServices::fromCoreState(state);

    services.setRuntimeValue(0, 1.5f);
    services.setRuntimeValue(1, -0.5f);

    assert(std::fabs(services.runtimeValue(0) - 1.0f) < 0.0001f);
    assert(std::fabs(services.runtimeValue(1) - 0.0f) < 0.0001f);

    drainNotifications();
    state.flush();

    std::cout << "[PASS] test_runtime_values_are_forwarded_and_clamped\n";
}

void test_config_changes_persist_and_bump_revision() {
    CoreStorages storage;

    uint8_t updatedChannel = 0;
    uint8_t updatedCc = 0;

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroWorkspace,
                                     storage.macroLibrary,
                                     storage.sequencerWorkspace,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);
        const auto services = core::handler::MacroDomainServices::fromCoreState(state);

        const auto initialConfig = services.activeConfig(0);
        const uint32_t initialRevision = state.configRevision.get();

        assert(!services.setConfig(0, initialConfig.channel, initialConfig.cc));
        assert(state.configRevision.get() == initialRevision);

        updatedChannel = static_cast<uint8_t>((initialConfig.channel + 1U) % 16U);
        updatedCc = static_cast<uint8_t>((initialConfig.cc < 127U) ? (initialConfig.cc + 1U)
                                                                   : (initialConfig.cc - 1U));

        assert(services.setConfig(0, updatedChannel, updatedCc));
        assert(state.configRevision.get() == initialRevision + 1U);

        const auto updatedConfig = services.activeConfig(0);
        assert(updatedConfig.channel == updatedChannel);
        assert(updatedConfig.cc == updatedCc);

        drainNotifications();
        state.flush();
    }

    core::state::CoreState restored(storage.settings,
                                    storage.macroWorkspace,
                                    storage.macroLibrary,
                                    storage.sequencerWorkspace,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);
    const auto restoredServices = core::handler::MacroDomainServices::fromCoreState(restored);
    const auto restoredConfig = restoredServices.activeConfig(0);
    assert(restoredConfig.channel == updatedChannel);
    assert(restoredConfig.cc == updatedCc);

    drainNotifications();

    std::cout << "[PASS] test_config_changes_persist_and_bump_revision\n";
}

void test_switch_to_page_updates_runtime_status_and_persists_workspace() {
    CoreStorages storage;
    storage.initAll();

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroWorkspace,
                                     storage.macroLibrary,
                                     storage.sequencerWorkspace,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);
        const auto services = core::handler::MacroDomainServices::fromCoreState(state);

        std::strncpy(state.pages.activeTrackData().pages[2].name,
                     "Mix Bus",
                     core::state::macro::PAGE_NAME_SIZE - 1);
        state.pages.activeTrackData().pages[2].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';
        state.pages.activeTrackData().pages[2].values[0] = 0.23f;

        services.switchToPage(2);

        assert(state.pages.activePage == 2);
        assert(std::strcmp(state.statusBar.pageName.get(), "Mix Bus") == 0);
        assert(std::fabs(services.runtimeValue(0) - 0.23f) < 0.0001f);

        drainNotifications();
        state.flush();
    }

    core::state::CoreState restored(storage.settings,
                                    storage.macroWorkspace,
                                    storage.macroLibrary,
                                    storage.sequencerWorkspace,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);
    const auto restoredServices = core::handler::MacroDomainServices::fromCoreState(restored);
    assert(restored.pages.activePage == 2);
    assert(std::strcmp(restored.statusBar.pageName.get(), "Mix Bus") == 0);
    assert(std::fabs(restoredServices.runtimeValue(0) - 0.23f) < 0.0001f);

    drainNotifications();

    std::cout << "[PASS] test_switch_to_page_updates_runtime_status_and_persists_workspace\n";
}

void test_status_bar_pulses_are_forwarded() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::MacroDomainServices::fromCoreState(state);

    services.pulseCcIn();
    services.pulseCcOut();
    services.pulseNoteIn();

    assert(state.statusBar.ccInActive.get());
    assert(state.statusBar.ccOutActive.get());
    assert(state.statusBar.noteInActive.get());

    drainNotifications();

    std::cout << "[PASS] test_status_bar_pulses_are_forwarded\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_runtime_values_are_forwarded_and_clamped();
    test_config_changes_persist_and_bump_revision();
    test_switch_to_page_updates_runtime_status_and_persists_workspace();
    test_status_bar_pulses_are_forwarded();
    std::cout << "\nAll MacroDomainServices tests passed.\n";
    return 0;
}

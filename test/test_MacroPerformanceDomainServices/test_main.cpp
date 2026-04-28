#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>

#include <oc/time/Time.hpp>

#include "../../src/handler/macro/MacroPerformanceDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
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
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

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
        const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

        const auto initialConfig = services.activeConfig(0);
        const uint32_t initialRevision = state.configRevision.get();

        assert(!services.setConfig(0, initialConfig.channel, initialConfig.cc));
        assert(state.configRevision.get() == initialRevision);

        updatedChannel = static_cast<uint8_t>((initialConfig.channel + 1U) % 16U);
        updatedCc = static_cast<uint8_t>((initialConfig.cc < 127U) ? (initialConfig.cc + 1U)
                                                                   : (initialConfig.cc - 1U));

        assert(services.setConfig(0, updatedChannel, updatedCc));
        assert(
            state.configRevision.get() ==
            core::state::macro::nextMacroConfigRevision(
                initialRevision,
                core::state::macro::kMacroConfigDirtyAll
            )
        );

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
    const auto restoredServices = core::handler::MacroPerformanceDomainServices::fromCoreState(
        restored
    );
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
        const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

        std::strncpy(state.pages.activeTrackData().pages[2].name,
                     "Mix Bus",
                     core::state::macro::PAGE_NAME_SIZE - 1);
        state.pages.activeTrackData().pages[2].name[core::state::macro::PAGE_NAME_SIZE - 1] = '\0';
        state.pages.activeTrackData().pages[2].values[0] = 0.23f;

        services.switchToPage(2);

        assert(state.pages.currentActivePage() == 2);
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
    const auto restoredServices = core::handler::MacroPerformanceDomainServices::fromCoreState(
        restored
    );
    assert(restored.pages.currentActivePage() == 2);
    assert(std::strcmp(restored.statusBar.pageName.get(), "Mix Bus") == 0);
    assert(std::fabs(restoredServices.runtimeValue(0) - 0.23f) < 0.0001f);

    drainNotifications();

    std::cout << "[PASS] test_switch_to_page_updates_runtime_status_and_persists_workspace\n";
}

void test_track_config_batch_requires_shared_channel_and_persists_when_valid() {
    CoreStorages storage;

    std::array<core::state::macro::MacroConfig, core::state::macro::MACRO_COUNT> updatedConfigs{};
    uint8_t updatedChannel = 0;

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroWorkspace,
                                     storage.macroLibrary,
                                     storage.sequencerWorkspace,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);
        const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
        const uint32_t initialRevision = state.configRevision.get();

        for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
            updatedConfigs[i] = services.activeConfig(i);
        }

        updatedConfigs[0].channel = 4;
        updatedConfigs[1].channel = 5;
        assert(!services.setTrackConfigs(updatedConfigs));
        assert(state.configRevision.get() == initialRevision);

        updatedChannel = 11;
        for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
            updatedConfigs[i].channel = updatedChannel;
            updatedConfigs[i].cc = static_cast<uint8_t>((32U + i) % 128U);
        }

        assert(services.setTrackConfigs(updatedConfigs));
        assert(state.configRevision.get() ==
               core::state::macro::nextMacroConfigRevision(initialRevision));
        assert(state.pages.activeTrackChannel() == updatedChannel);
        for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
            const auto config = services.activeConfig(i);
            assert(config.channel == updatedChannel);
            assert(config.cc == updatedConfigs[i].cc);
        }

        drainNotifications();
        state.flush();
    }

    core::state::CoreState restored(storage.settings,
                                    storage.macroWorkspace,
                                    storage.macroLibrary,
                                    storage.sequencerWorkspace,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);
    const auto restoredServices = core::handler::MacroPerformanceDomainServices::fromCoreState(
        restored
    );
    for (uint8_t i = 0; i < core::state::macro::MACRO_COUNT; ++i) {
        const auto config = restoredServices.activeConfig(i);
        assert(config.channel == updatedChannel);
        assert(config.cc == updatedConfigs[i].cc);
    }

    std::cout << "[PASS] test_track_config_batch_requires_shared_channel_and_persists_when_valid\n";
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
    const auto services = core::handler::MacroPerformanceDomainServices::fromCoreState(state);

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
    test_track_config_batch_requires_shared_channel_and_persists_when_valid();
    test_status_bar_pulses_are_forwarded();
    std::cout << "\nAll MacroPerformanceDomainServices tests passed.\n";
    return 0;
}

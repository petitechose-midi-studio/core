#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

#include <oc/time/Time.hpp>

#include "../../src/handler/settings/DataManagerDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/sequencer/SequencerPersistenceWorkflow.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {
using test_support::CoreStorages;
using test_support::drainNotifications;

uint32_t g_now_ms = 0;

uint32_t mockTimeMs() {
    return g_now_ms;
}

void test_slot_counts_match_persistence_domains() {
    CoreStorages storage;

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::DataManagerDomainServices::fromCoreState(state);

    assert(services.slotCount(core::state::DataManagerCommand::NONE) == 0);
    assert(services.slotCount(core::state::DataManagerCommand::MACRO_SAVE_SLOT) ==
           core::persistence::MacroPersistence::LIBRARY_SLOT_COUNT);
    assert(services.slotCount(core::state::DataManagerCommand::SEQ_SAVE_PATTERN_SLOT) ==
           core::persistence::SequencerPersistence::PATTERN_LIBRARY_SLOT_COUNT);
    assert(services.slotCount(core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT) ==
           core::persistence::SequencerPersistence::SET_LIBRARY_SLOT_COUNT);

    std::cout << "[PASS] test_slot_counts_match_persistence_domains\n";
}

void test_shortcuts_persist_and_sanitize_through_domain_service() {
    CoreStorages storage;
    storage.initAll();

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroWorkspace,
                                     storage.macroLibrary,
                                     storage.sequencerWorkspace,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);
        const auto services = core::handler::DataManagerDomainServices::fromCoreState(state);

        services.setShortcut(core::state::DataManagerContext::MACRO,
                             true,
                             core::state::DataManagerCommand::MACRO_ERASE_SLOT);
        services.setShortcut(core::state::DataManagerContext::MACRO,
                             false,
                             core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT);
        services.setShortcut(core::state::DataManagerContext::SEQUENCER,
                             true,
                             core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT);
        services.setShortcut(core::state::DataManagerContext::SEQUENCER,
                             false,
                             core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT);
    }

    core::state::CoreState restored(storage.settings,
                                    storage.macroWorkspace,
                                    storage.macroLibrary,
                                    storage.sequencerWorkspace,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);
    assert(restored.dataManager.macroShortcutLeft.get() ==
           core::state::DataManagerCommand::MACRO_ERASE_SLOT);
    assert(restored.dataManager.macroShortcutRight.get() ==
           core::state::DEFAULT_MACRO_SHORTCUT_RIGHT);
    assert(restored.dataManager.seqShortcutLeft.get() ==
           core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT);
    assert(restored.dataManager.seqShortcutRight.get() ==
           core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT);

    drainNotifications();

    std::cout << "[PASS] test_shortcuts_persist_and_sanitize_through_domain_service\n";
}

void test_macro_slot_execution_and_probe_roundtrip() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::DataManagerDomainServices::fromCoreState(state);

    core::state::macro::MacroWorkflow::setRuntimeValue(state, 0, 0.73f);
    drainNotifications();
    state.flush();

    assert(!services.slotOccupied(core::state::DataManagerCommand::MACRO_SAVE_SLOT, 5));

    const auto save = services.execute(core::state::DataManagerCommand::MACRO_SAVE_SLOT,
                                       5,
                                       core::state::DataManagerSetLoadMode::REPLACE);
    assert(save.handled);
    assert(save.success);
    assert(!save.isLoadOperation);
    assert(!save.deferredApply);

    assert(services.slotOccupied(core::state::DataManagerCommand::MACRO_LOAD_SLOT, 5));

    core::state::macro::MacroWorkflow::setRuntimeValue(state, 0, 0.11f);
    drainNotifications();
    state.flush();

    const auto load = services.execute(core::state::DataManagerCommand::MACRO_LOAD_SLOT,
                                       5,
                                       core::state::DataManagerSetLoadMode::REPLACE);
    assert(load.handled);
    assert(load.success);
    assert(load.isLoadOperation);
    assert(load.loadStatus == core::persistence::SlotLoadStatus::OK);

    const float restored = core::state::macro::MacroWorkflow::runtimeValue(state, 0);
    assert(std::fabs(restored - 0.73f) < 0.0001f);

    const auto none = services.execute(core::state::DataManagerCommand::NONE,
                                       0,
                                       core::state::DataManagerSetLoadMode::REPLACE);
    assert(!none.handled);
    assert(!none.success);

    drainNotifications();

    std::cout << "[PASS] test_macro_slot_execution_and_probe_roundtrip\n";
}

void test_sequencer_set_load_reports_deferred_apply() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroWorkspace,
                                 storage.macroLibrary,
                                 storage.sequencerWorkspace,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::DataManagerDomainServices::fromCoreState(state);

    state.sequencer.length.set(8);
    state.sequencer.enabledMask.set({});
    state.sequencer.setStepDataAt(2, 72, 110, 45);
    state.sequencer.toggle(2);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::saveSetSlot(state, 6));

    state.sequencer.length.set(16);
    state.sequencer.enabledMask.set({});
    state.sequencer.setStepDataAt(1, 48, 64, 55);
    state.sequencer.toggle(1);
    state.sequencer.playheadStep.set(7);
    state.statusBar.playing.set(true);

    const auto load = services.execute(core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT,
                                       6,
                                       core::state::DataManagerSetLoadMode::MERGE);
    assert(load.handled);
    assert(load.success);
    assert(load.isLoadOperation);
    assert(load.deferredApply);
    assert(load.loadStatus == core::persistence::SlotLoadStatus::OK);
    assert(state.hasPendingSequencerApply());
    assert(state.sequencer.length.get() == 16);

    drainNotifications();

    std::cout << "[PASS] test_sequencer_set_load_reports_deferred_apply\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    test_slot_counts_match_persistence_domains();
    test_shortcuts_persist_and_sanitize_through_domain_service();
    test_macro_slot_execution_and_probe_roundtrip();
    test_sequencer_set_load_reports_deferred_apply();
    std::cout << "\nAll DataManagerDomainServices tests passed.\n";
    return 0;
}

#include <cassert>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <utility>
#include <vector>

#include "../../src/handler/macro/MacroPerformanceDomainServices.hpp"
#include "../../src/handler/settings/DataManagerDomainServices.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/DataManagerWorkflow.hpp"
#include "../../src/state/macro/MacroPersistenceWorkflow.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/sequencer/SequencerCcLanePatternOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../../src/state/sequencer/SequencerPersistenceWorkflow.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/NotificationTestUtils.hpp"

namespace {
using test_support::CoreStorages;
using test_support::drainNotifications;

void setManualMacroValue(core::state::CoreState& state, uint8_t index, float value) {
    core::handler::MacroPerformanceDomainServices::fromCoreState(state).setManualValue(
        index,
        value
    );
}

void addGraphContent(core::state::sequencer::SequencerPatternState& pattern,
                     int8_t noteOffset = 7) {
    using namespace core::state::sequencer;

    const auto micro = createMicroSequence(pattern, rootStepNodeId(2), 3);
    assert(micro.ok);
    const auto* graph = graphView(pattern);
    assert(graph != nullptr);
    const auto* sequence = graph->sequence(micro.id);
    assert(sequence != nullptr);
    assert(setNodeNoteOffset(
        pattern,
        static_cast<uint16_t>(sequence->firstStepNode + 1),
        noteOffset
    ));
}

void addCcLane(core::state::sequencer::SequencerPatternState& pattern,
               uint8_t lane,
               uint8_t controller,
               uint8_t step,
               uint8_t value) {
    namespace seq = core::state::sequencer;
    auto* bank = seq::ensureSequencerCcLaneBank(pattern);
    assert(bank != nullptr);
    const seq::SequencerCcLaneDraft draft{
        .destination = seq::SequencerCcLaneDestination{
            .controller = controller,
            .minimum = 0,
            .maximum = 127,
            .routePolicy = seq::SequencerCcLaneRoutePolicy::INHERIT_TRACK,
            .pinnedPort = 0,
            .pinnedChannel = 0,
        },
        .initialValue = 64,
    };
    assert(seq::createSequencerCcLane(*bank, lane, draft).changed());
    assert(seq::setSequencerCcLaneEvent(*bank, lane, step, value).changed());
    pattern.ccLaneRevision.set(bank->revision);
}

bool hasGraphContent(const core::state::sequencer::SequencerPatternState& pattern,
                     int8_t noteOffset = 7) {
    using namespace core::state::sequencer;

    const auto* graph = graphView(pattern);
    if (graph == nullptr) return false;
    const auto* root = graph->stepNode(rootStepNodeId(2));
    if (root == nullptr || !root->has(oc::note::sequencer::STEP_NODE_CHILD_SEQUENCE)) {
        return false;
    }
    const auto* sequence = graph->sequence(root->childSequenceId);
    if (sequence == nullptr || sequence->length != 3) return false;
    const auto* child = graph->stepNode(static_cast<uint16_t>(sequence->firstStepNode + 1));
    return child != nullptr &&
           child->has(oc::note::sequencer::STEP_NODE_NOTE_OFFSET) &&
           child->noteOffset == noteOffset;
}

void recordLengthHistory(core::state::CoreState& state, uint8_t nextLength) {
    core::state::sequencer::SequencerHistoryPatternSnapshot before;
    assert(core::state::sequencer::captureHistorySnapshot(state.sequencer, before));

    state.sequencer.pattern.length.set(nextLength);

    core::state::sequencer::SequencerHistoryPatternSnapshot after;
    assert(core::state::sequencer::captureHistorySnapshot(state.sequencer, after));
    assert(state.recordSequencerPatternHistory(
        std::move(before),
        std::move(after),
        core::state::sequencer::SequencerHistoryDescriptor{
            .kind = core::state::sequencer::SequencerHistoryActionKind::QuickControls,
        }
    ));
}

void test_project_state_does_not_boot_restore_from_removed_domain_store() {
    CoreStorages storage;

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroLibrary,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);
        setManualMacroValue(state, 0, 0.13f);
        setManualMacroValue(state, 1, 0.87f);
        oc::state::NotificationQueue::instance().flush();
        state.flush();
    }

    // Corrupt core settings storage only.
    storage.settings.erase(0, storage.settings.capacity());

    core::state::CoreState restored(storage.settings,
                                    storage.macroLibrary,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);
    assert(core::state::macro::MacroWorkflow::runtimeValue(restored.macros, 0) != 0.13f);
    assert(core::state::macro::MacroWorkflow::runtimeValue(restored.macros, 1) != 0.87f);

    drainNotifications();

    std::cout << "[PASS] test_project_state_does_not_boot_restore_from_removed_domain_store\n";
}

void test_macro_library_roundtrip_and_erase() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    core::state::macro::MacroWorkflow::switchToPage(state, 2);
    core::state::macro::MacroWorkflow::setConfig(state, 0, 4, 88);
    setManualMacroValue(state, 0, 0.64f);
    oc::state::NotificationQueue::instance().flush();
    state.flush();

    assert(core::state::macro::MacroPersistenceWorkflow::saveLibrarySlot(state, 3));

    core::state::macro::MacroWorkflow::setConfig(state, 0, 0, 1);
    setManualMacroValue(state, 0, 0.01f);
    oc::state::NotificationQueue::instance().flush();
    state.flush();

    const auto status = core::state::macro::MacroPersistenceWorkflow::loadLibrarySlot(state, 3);
    assert(status == core::persistence::SlotLoadStatus::OK);
    assert(state.pages.currentActivePage() == 2);
    assert(core::state::macro::MacroWorkflow::activeConfig(state.pages, 0).channel == 4);
    assert(core::state::macro::MacroWorkflow::activeConfig(state.pages, 0).cc == 88);
    assert(core::state::macro::MacroWorkflow::runtimeValue(state.macros, 0) == 0.64f);

    assert(core::state::macro::MacroPersistenceWorkflow::eraseLibrarySlot(state, 3));
    const auto erasedStatus = core::state::macro::MacroPersistenceWorkflow::loadLibrarySlot(
        state,
        3
    );
    assert(erasedStatus == core::persistence::SlotLoadStatus::EMPTY);

    drainNotifications();

    std::cout << "[PASS] test_macro_library_roundtrip_and_erase\n";
}

void test_macro_library_save_uses_manual_base_without_flush() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    // Manual input updates the base value immediately; project mutation stays
    // coalesced and does not need to flush before an explicit library save.
    setManualMacroValue(state, 0, 0.37f);
    core::state::macro::MacroWorkflow::setRuntimeValue(state.macros, 0, 0.91f);
    assert(state.pages.activePageData().values[0] == 0.37f);
    assert(core::state::macro::MacroPersistenceWorkflow::saveLibrarySlot(state, 6));

    // Move away from that value so load verification is unambiguous.
    setManualMacroValue(state, 0, 0.02f);
    oc::state::NotificationQueue::instance().flush();
    state.flush();

    const auto status = core::state::macro::MacroPersistenceWorkflow::loadLibrarySlot(state, 6);
    assert(status == core::persistence::SlotLoadStatus::OK);

    const float restored = core::state::macro::MacroWorkflow::runtimeValue(state.macros, 0);
    assert(restored > 0.3699f && restored < 0.3701f);

    drainNotifications();

    std::cout << "[PASS] test_macro_library_save_uses_manual_base_without_flush\n";
}

void test_macro_config_changes_mark_project_dirty_and_bump_revision() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    const auto& initialConfig = core::state::macro::MacroWorkflow::activeConfig(state.pages, 0);
    const uint32_t initialRevision = state.configRevision.get();

    assert(!core::state::macro::MacroWorkflow::setConfig(
        state,
        0,
        initialConfig.channel,
        initialConfig.cc
    ));
    assert(state.configRevision.get() == initialRevision);

    const uint8_t updatedChannel = static_cast<uint8_t>((initialConfig.channel + 1U) % 16U);
    const uint8_t updatedCc = static_cast<uint8_t>((initialConfig.cc < 127U) ? (initialConfig.cc + 1U)
                                                                            : (initialConfig.cc - 1U));

    assert(core::state::macro::MacroWorkflow::setConfig(state, 0, updatedChannel, updatedCc));
    assert(
        state.configRevision.get() ==
        core::state::macro::nextMacroConfigRevision(
            initialRevision,
            core::state::macro::kMacroConfigDirtyAll
        )
    );
    assert(state.project.metadata.dirty);
    assert(state.hasPendingProjectSessionSave());

    const auto& updatedConfig = core::state::macro::MacroWorkflow::activeConfig(state.pages, 0);
    assert(updatedConfig.channel == updatedChannel);
    assert(updatedConfig.cc == updatedCc);

    drainNotifications();

    std::cout << "[PASS] test_macro_config_changes_mark_project_dirty_and_bump_revision\n";
}

void test_macro_config_changes_do_not_require_macro_library_storage() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    const auto& initialConfig = core::state::macro::MacroWorkflow::activeConfig(state.pages, 0);
    const uint8_t updatedChannel = static_cast<uint8_t>((initialConfig.channel + 2U) % 16U);
    const uint8_t updatedCc = static_cast<uint8_t>((initialConfig.cc + 3U) % 128U);

    storage.macroLibrary.setAvailable(false);
    assert(core::state::macro::MacroWorkflow::setConfig(state, 0, updatedChannel, updatedCc));
    assert(state.project.metadata.dirty);
    assert(state.hasPendingProjectSessionSave());

    drainNotifications();

    std::cout << "[PASS] test_macro_config_changes_do_not_require_macro_library_storage\n";
}

void test_shared_track_pending_save_survives_settings_storage_unavailable() {
    CoreStorages storage;
    storage.initAll();

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroLibrary,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);

        storage.settings.setAvailable(false);
        assert(state.setSharedTrackState(0x0003, 1));
        state.flush();

        storage.settings.setAvailable(true);
        state.flush();
    }

    core::state::CoreState restored(storage.settings,
                                    storage.macroLibrary,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);
    assert(restored.currentSharedTrackEnabledMask() == 0x0003);
    assert(restored.currentSharedActiveTrack() == 1);

    drainNotifications();

    std::cout << "[PASS] test_shared_track_pending_save_survives_settings_storage_unavailable\n";
}

void test_recovery_from_ram_after_storage_reopen_does_not_reload_stale_card_data() {
    CoreStorages storage;
    storage.initAll();

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroLibrary,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);

        core::state::macro::MacroWorkflow::setConfig(state, 0, 1, 10);
        state.sequencer.pattern.length.set(8);
        state.sequencer.setStepDataAt(0, 60, 80, 50);
        state.sequencer.pattern.toggle(0);
        drainNotifications();
        state.flush();
    }

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroLibrary,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);

        storage.settings.setAvailable(false);
        storage.macroLibrary.setAvailable(false);
        storage.sequencerPatternLibrary.setAvailable(false);
        storage.sequencerSetLibrary.setAvailable(false);

        assert(state.setSharedTrackState(0x0003, 1));
        assert(core::state::macro::MacroWorkflow::setConfig(state, 0, 3, 88));
        state.sequencer.pattern.length.set(16);
        state.sequencer.setStepDataAt(0, 72, 111, 75);
        if (!state.sequencer.pattern.isEnabled(0)) {
            state.sequencer.pattern.toggle(0);
        }

        core::state::DataManagerWorkflow::setShortcut(
            {state.dataManager, state.settings},
            core::state::DataManagerContext::MACRO,
            true,
            core::state::DataManagerCommand::MACRO_ERASE_SLOT
        );

        storage.settings.setAvailable(true);
        storage.macroLibrary.setAvailable(true);
        storage.sequencerPatternLibrary.setAvailable(true);
        storage.sequencerSetLibrary.setAvailable(true);

        assert(state.recoverPersistenceFromRamAfterStorageReopen() ==
               core::persistence::PersistenceWriteStatus::OK);
    }

    core::state::CoreState restored(storage.settings,
                                    storage.macroLibrary,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);

    const auto& restoredConfig = core::state::macro::MacroWorkflow::activeConfig(restored.pages, 0);
    assert(restoredConfig.channel != 3 || restoredConfig.cc != 88);
    assert(restored.sequencer.pattern.note[0] != 72 ||
           restored.sequencer.pattern.velocity[0] != 111 ||
           restored.sequencer.pattern.gate[0] != 75 ||
           !restored.sequencer.pattern.isEnabled(0));
    assert(restored.currentSharedTrackEnabledMask() == 0x0003);
    assert(restored.currentSharedActiveTrack() == 1);
    assert(restored.dataManager.macroShortcutLeft.get() ==
           core::state::DataManagerCommand::MACRO_ERASE_SLOT);

    drainNotifications();

    std::cout << "[PASS] test_recovery_from_ram_after_storage_reopen_does_not_reload_stale_card_data\n";
}

void test_data_manager_shortcuts_persist_and_sanitize() {
    CoreStorages storage;
    storage.initAll();

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroLibrary,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);

        auto refs = core::state::DataManagerWorkflow::StateRefs{
            state.dataManager,
            state.settings,
        };

        core::state::DataManagerWorkflow::setShortcut(refs,
                                                      core::state::DataManagerContext::MACRO,
                                                      true,
                                                      core::state::DataManagerCommand::MACRO_ERASE_SLOT);
        // Cross-context mapping should sanitize to macro default right shortcut.
        core::state::DataManagerWorkflow::setShortcut(
            refs,
            core::state::DataManagerContext::MACRO,
            false,
            core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT
        );

        core::state::DataManagerWorkflow::setShortcut(
            refs,
            core::state::DataManagerContext::SEQUENCER,
            true,
            core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT
        );
        core::state::DataManagerWorkflow::setShortcut(
            refs,
            core::state::DataManagerContext::SEQUENCER,
            false,
            core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT
        );
    }

    core::state::CoreState restored(storage.settings,
                                    storage.macroLibrary,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);

    assert(restored.dataManager.macroShortcutLeft.get() == core::state::DataManagerCommand::MACRO_ERASE_SLOT);
    assert(restored.dataManager.macroShortcutRight.get() == core::state::DEFAULT_MACRO_SHORTCUT_RIGHT);
    assert(restored.dataManager.seqShortcutLeft.get() == core::state::DataManagerCommand::SEQ_SAVE_SET_SLOT);
    assert(restored.dataManager.seqShortcutRight.get() == core::state::DataManagerCommand::SEQ_LOAD_SET_SLOT);

    drainNotifications();

    std::cout << "[PASS] test_data_manager_shortcuts_persist_and_sanitize\n";
}

void test_data_manager_command_execution_and_slot_probe() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    const auto services = core::handler::DataManagerDomainServices::fromCoreState(state);

    setManualMacroValue(state, 0, 0.73f);
    oc::state::NotificationQueue::instance().flush();
    state.flush();

    assert(!services.slotOccupied(
        core::state::DataManagerCommand::MACRO_SAVE_SLOT,
        5
    ));

    const auto save = services.execute(
        core::state::DataManagerCommand::MACRO_SAVE_SLOT,
        5,
        core::state::DataManagerSetLoadMode::REPLACE
    );
    assert(save.handled);
    assert(save.success);
    assert(!save.isLoadOperation);

    assert(services.slotOccupied(
        core::state::DataManagerCommand::MACRO_SAVE_SLOT,
        5
    ));

    setManualMacroValue(state, 0, 0.11f);
    oc::state::NotificationQueue::instance().flush();
    state.flush();

    const auto load = services.execute(
        core::state::DataManagerCommand::MACRO_LOAD_SLOT,
        5,
        core::state::DataManagerSetLoadMode::REPLACE
    );
    assert(load.handled);
    assert(load.success);
    assert(load.isLoadOperation);
    assert(load.loadStatus == core::persistence::SlotLoadStatus::OK);

    const float restored = core::state::macro::MacroWorkflow::runtimeValue(state.macros, 0);
    assert(restored > 0.7299f && restored < 0.7301f);

    const auto emptyLoad = services.execute(
        core::state::DataManagerCommand::SEQ_LOAD_PATTERN_SLOT,
        31,
        core::state::DataManagerSetLoadMode::REPLACE
    );
    assert(emptyLoad.handled);
    assert(!emptyLoad.success);
    assert(emptyLoad.isLoadOperation);
    assert(emptyLoad.loadStatus == core::persistence::SlotLoadStatus::EMPTY);

    const auto none = services.execute(
        core::state::DataManagerCommand::NONE,
        0,
        core::state::DataManagerSetLoadMode::REPLACE
    );
    assert(!none.handled);
    assert(!none.success);

    drainNotifications();

    std::cout << "[PASS] test_data_manager_command_execution_and_slot_probe\n";
}

void test_sequencer_library_roundtrip() {
    CoreStorages storage;
    storage.initAll();

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroLibrary,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);

        state.sequencer.pattern.length.set(16);
        state.sequencer.pattern.stepsPerBeat.set(4);
        state.sequencer.pattern.midiChannel.set(3);
        state.sequencer.pattern.toggle(0);
        state.sequencer.setStepDataAt(0, 64, 120, 70);
        state.sequencer.setStepProbabilityAt(0, 42);

        oc::state::NotificationQueue::instance().flush();
        state.flush();

        assert(core::state::sequencer::SequencerPersistenceWorkflow::savePatternSlot(state, 4));
        assert(core::state::sequencer::SequencerPersistenceWorkflow::saveSetSlot(state, 2));

        state.sequencer.pattern.length.set(8);
        state.sequencer.pattern.stepsPerBeat.set(2);
        state.sequencer.pattern.midiChannel.set(0);
        state.sequencer.pattern.enabledMask.set({});
        state.sequencer.setStepDataAt(0, 40, 40, 40);
        oc::state::NotificationQueue::instance().flush();
        state.flush();

        const auto patternStatus =
            core::state::sequencer::SequencerPersistenceWorkflow::loadPatternSlot(state, 4);
        assert(patternStatus == core::persistence::SlotLoadStatus::OK);
        assert(state.sequencer.pattern.length.get() == 16);
        assert(state.sequencer.pattern.stepsPerBeat.get() == 4);
        assert(state.sequencer.pattern.midiChannel.get() == 3);
        assert(state.sequencer.pattern.isEnabled(0));
        assert(state.sequencer.pattern.note[0] == 64);
        assert(state.sequencer.pattern.velocity[0] == 120);
        assert(state.sequencer.pattern.gate[0] == 70);
        assert(state.sequencer.pattern.probability[0] == 42);

        assert(core::state::sequencer::SequencerPersistenceWorkflow::erasePatternSlot(state, 4));
        const auto erasedPatternStatus =
            core::state::sequencer::SequencerPersistenceWorkflow::loadPatternSlot(state, 4);
        assert(erasedPatternStatus == core::persistence::SlotLoadStatus::EMPTY);

        const auto setStatus =
            core::state::sequencer::SequencerPersistenceWorkflow::loadSetSlot(state, 2);
        assert(setStatus == core::persistence::SlotLoadStatus::OK);
        assert(state.sequencer.pattern.length.get() == 16);
        assert(state.sequencer.pattern.stepsPerBeat.get() == 4);
        assert(state.sequencer.pattern.midiChannel.get() == 3);

        assert(core::state::sequencer::SequencerPersistenceWorkflow::eraseSetSlot(state, 2));
        const auto erasedSetStatus =
            core::state::sequencer::SequencerPersistenceWorkflow::loadSetSlot(state, 2);
        assert(erasedSetStatus == core::persistence::SlotLoadStatus::EMPTY);

        drainNotifications();
    }

    drainNotifications();

    std::cout << "[PASS] test_sequencer_library_roundtrip\n";
}

void test_sequencer_navigation_context_does_not_boot_restore_without_session() {
    CoreStorages storage;
    storage.initAll();

    {
        core::state::CoreState state(storage.settings,
                                     storage.macroLibrary,
                                     storage.sequencerPatternLibrary,
                                     storage.sequencerSetLibrary);

        state.sequencer.pattern.length.set(24);
        state.sequencer.focusedStep.set(10);
        state.sequencer.page.set(state.sequencer.pageForStep(10));
        state.sequencer.activeStepProperty.set(core::state::sequencer::StepProperty::GATE);

        oc::state::NotificationQueue::instance().flush();
        state.flush();
    }

    core::state::CoreState restored(storage.settings,
                                    storage.macroLibrary,
                                    storage.sequencerPatternLibrary,
                                    storage.sequencerSetLibrary);

    assert(restored.sequencer.pattern.length.get() != 24);
    assert(restored.sequencer.focusedStep.get() != 10 ||
           restored.sequencer.activeStepProperty.get() != core::state::sequencer::StepProperty::GATE);

    drainNotifications();

    std::cout << "[PASS] test_sequencer_navigation_context_does_not_boot_restore_without_session\n";
}

void test_sequencer_load_is_quantized_to_next_step_when_playing() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    // Pattern A (will be saved and reloaded)
    state.sequencer.pattern.length.set(8);
    state.sequencer.pattern.stepsPerBeat.set(2);
    state.sequencer.pattern.midiChannel.set(1);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(0, 61, 101, 80);
    state.sequencer.pattern.toggle(0);
    oc::state::NotificationQueue::instance().flush();
    state.flush();
    assert(core::state::sequencer::SequencerPersistenceWorkflow::savePatternSlot(state, 1));

    // Pattern B (current live state before queued load)
    state.sequencer.pattern.length.set(16);
    state.sequencer.pattern.stepsPerBeat.set(4);
    state.sequencer.pattern.midiChannel.set(6);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(0, 40, 55, 30);
    state.sequencer.pattern.toggle(0);

    state.statusBar.playing.set(true);
    state.sequencer.playheadStep.set(5);

    const auto queuedStatus =
        core::state::sequencer::SequencerPersistenceWorkflow::loadPatternSlot(state, 1);
    assert(queuedStatus == core::persistence::SlotLoadStatus::OK);

    // Load is deferred: no immediate replacement while still on same step.
    assert(state.sequencer.pattern.length.get() == 16);
    assert(state.sequencer.pattern.note[0] == 40);

    state.update();
    assert(state.sequencer.pattern.length.get() == 16);
    assert(state.sequencer.pattern.note[0] == 40);

    // Next step reached -> queued apply must happen.
    state.sequencer.playheadStep.set(6);
    state.update();
    assert(state.sequencer.pattern.length.get() == 8);
    assert(state.sequencer.pattern.stepsPerBeat.get() == 2);
    assert(state.sequencer.pattern.midiChannel.get() == 1);
    assert(state.sequencer.pattern.note[0] == 61);
    assert(state.sequencer.pattern.velocity[0] == 101);
    assert(state.sequencer.pattern.gate[0] == 80);

    // Same behavior for set library loads.
    assert(core::state::sequencer::SequencerPersistenceWorkflow::saveSetSlot(state, 2));

    state.sequencer.pattern.length.set(12);
    state.sequencer.setStepDataAt(0, 77, 77, 77);
    state.sequencer.playheadStep.set(9);

    const auto queuedSetStatus =
        core::state::sequencer::SequencerPersistenceWorkflow::loadSetSlot(state, 2);
    assert(queuedSetStatus == core::persistence::SlotLoadStatus::OK);
    assert(state.sequencer.pattern.length.get() == 12);
    assert(state.sequencer.pattern.note[0] == 77);

    state.sequencer.playheadStep.set(10);
    state.update();
    assert(state.sequencer.pattern.length.get() == 8);
    assert(state.sequencer.pattern.note[0] == 61);

    drainNotifications();

    std::cout << "[PASS] test_sequencer_load_is_quantized_to_next_step_when_playing\n";
}

void test_sequencer_queued_pattern_load_preserves_graph_content() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    state.sequencer.pattern.length.set(8);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(0, 61, 101, 80);
    state.sequencer.pattern.toggle(0);
    addGraphContent(state.sequencer.pattern);
    assert(hasGraphContent(state.sequencer.pattern));
    assert(core::state::sequencer::SequencerPersistenceWorkflow::savePatternSlot(state, 9));

    core::state::sequencer::clearGraph(state.sequencer.pattern);
    assert(!hasGraphContent(state.sequencer.pattern));

    state.statusBar.playing.set(true);
    state.sequencer.playheadStep.set(4);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::loadPatternSlot(state, 9) ==
           core::persistence::SlotLoadStatus::OK);
    assert(!hasGraphContent(state.sequencer.pattern));

    state.update();
    assert(!hasGraphContent(state.sequencer.pattern));

    state.sequencer.playheadStep.set(5);
    state.update();
    assert(hasGraphContent(state.sequencer.pattern));
    const auto& activeTrack = state.sequencerTracks.track(
        state.sequencerTracks.activeTrackIndex()
    );
    assert(hasGraphContent(activeTrack));
    assert(core::state::sequencer::graphView(state.sequencer.pattern) !=
           core::state::sequencer::graphView(activeTrack));

    drainNotifications();

    std::cout << "[PASS] test_sequencer_queued_pattern_load_preserves_graph_content\n";
}

void test_direct_load_clears_stale_pending_quantized_apply() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    // Slot 1: queued while playing.
    state.sequencer.pattern.length.set(8);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(0, 61, 101, 80);
    state.sequencer.pattern.toggle(0);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::savePatternSlot(state, 1));

    // Slot 2: loaded explicitly after transport stops.
    state.sequencer.pattern.length.set(12);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(0, 72, 88, 44);
    state.sequencer.pattern.toggle(0);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::savePatternSlot(state, 2));

    // Queue slot 1 while transport is running.
    state.statusBar.playing.set(true);
    state.sequencer.playheadStep.set(4);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::loadPatternSlot(state, 1) ==
           core::persistence::SlotLoadStatus::OK);

    // Stop before the queued apply is consumed, then load slot 2 directly.
    state.statusBar.playing.set(false);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::loadPatternSlot(state, 2) ==
           core::persistence::SlotLoadStatus::OK);
    assert(state.sequencer.pattern.length.get() == 12);
    assert(state.sequencer.pattern.note[0] == 72);

    // A later update must not replay the stale queued load from slot 1.
    state.update();
    assert(state.sequencer.pattern.length.get() == 12);
    assert(state.sequencer.pattern.note[0] == 72);
    assert(state.sequencer.pattern.velocity[0] == 88);
    assert(state.sequencer.pattern.gate[0] == 44);

    drainNotifications();

    std::cout << "[PASS] test_direct_load_clears_stale_pending_quantized_apply\n";
}

void test_sequencer_save_and_erase_keep_history() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    state.sequencer.pattern.length.set(8);
    recordLengthHistory(state, 12);
    assert(state.sequencerHistory.undoCount() == 1);

    assert(core::state::sequencer::SequencerPersistenceWorkflow::savePatternSlot(state, 10));
    assert(state.sequencerHistory.undoCount() == 1);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::erasePatternSlot(state, 10));
    assert(state.sequencerHistory.undoCount() == 1);

    assert(core::state::sequencer::SequencerPersistenceWorkflow::saveSetSlot(state, 10));
    assert(state.sequencerHistory.undoCount() == 1);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::eraseSetSlot(state, 10));
    assert(state.sequencerHistory.undoCount() == 1);

    drainNotifications();

    std::cout << "[PASS] test_sequencer_save_and_erase_keep_history\n";
}

void test_sequencer_load_clears_history_after_apply() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    state.sequencer.pattern.length.set(8);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::savePatternSlot(state, 11));
    assert(core::state::sequencer::SequencerPersistenceWorkflow::saveSetSlot(state, 11));

    recordLengthHistory(state, 12);
    assert(state.sequencerHistory.undoCount() == 1);

    assert(core::state::sequencer::SequencerPersistenceWorkflow::loadPatternSlot(state, 11) ==
           core::persistence::SlotLoadStatus::OK);
    assert(state.sequencer.pattern.length.get() == 8);
    assert(state.sequencerHistory.undoCount() == 0);
    assert(state.sequencerHistory.redoCount() == 0);
    assert(!state.undoSequencerHistory());

    recordLengthHistory(state, 16);
    assert(state.sequencerHistory.undoCount() == 1);

    assert(core::state::sequencer::SequencerPersistenceWorkflow::loadSetSlot(state, 11) ==
           core::persistence::SlotLoadStatus::OK);
    assert(state.sequencer.pattern.length.get() == 8);
    assert(state.sequencerHistory.undoCount() == 0);
    assert(state.sequencerHistory.redoCount() == 0);

    drainNotifications();

    std::cout << "[PASS] test_sequencer_load_clears_history_after_apply\n";
}

void test_queued_sequencer_load_clears_history_when_applied() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    state.sequencer.pattern.length.set(8);
    addCcLane(state.sequencer.pattern, 0, 74, 3, 92);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::savePatternSlot(state, 12));

    core::state::sequencer::installSequencerCcLaneBank(
        state.sequencer.pattern,
        {}
    );
    assert(core::state::sequencer::sequencerCcLaneView(state.sequencer.pattern) == nullptr);

    recordLengthHistory(state, 16);
    assert(state.sequencerHistory.undoCount() == 1);

    state.statusBar.playing.set(true);
    state.sequencer.playheadStep.set(3);

    assert(core::state::sequencer::SequencerPersistenceWorkflow::loadPatternSlot(state, 12) ==
           core::persistence::SlotLoadStatus::OK);
    assert(state.hasPendingSequencerApply());
    assert(state.sequencerHistory.undoCount() == 1);

    state.update();
    assert(state.sequencer.pattern.length.get() == 16);
    assert(state.sequencerHistory.undoCount() == 1);

    state.sequencer.playheadStep.set(4);
    state.update();
    assert(state.sequencer.pattern.length.get() == 8);
    const auto* restored =
        core::state::sequencer::sequencerCcLaneView(state.sequencer.pattern);
    assert(restored != nullptr);
    assert(restored->lanes[0].destination.controller == 74);
    assert(restored->lanes[0].activeMask.test(3));
    assert(restored->lanes[0].values[3] == 92);
    const auto* activeTrackCopy = core::state::sequencer::sequencerCcLaneView(
        state.sequencerTracks.track(state.sequencerTracks.activeTrackIndex())
    );
    assert(activeTrackCopy != nullptr);
    assert(activeTrackCopy->lanes[0].values[3] == 92);
    assert(state.sequencerHistory.undoCount() == 0);
    assert(state.sequencerHistory.redoCount() == 0);

    drainNotifications();

    std::cout << "[PASS] test_queued_sequencer_load_clears_history_when_applied\n";
}

void test_queued_full_bank_apply_preserves_every_cc_lane_payload() {
    CoreStorages storage;
    storage.initAll();
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    core::state::sequencer::SequencerTrackBankState stagedBank;
    core::state::sequencer::SequencerState staged;
    addCcLane(staged.pattern, 0, 74, 0, 81);
    addCcLane(stagedBank.track(0), 0, 74, 0, 81);
    addCcLane(stagedBank.track(5), 1, 71, 7, 103);

    state.statusBar.playing.set(true);
    state.sequencer.playheadStep.set(2);
    assert(state.queuePendingSequencerBankApply(stagedBank, staged));
    assert(state.hasPendingSequencerApply());
    state.sequencer.playheadStep.set(3);
    state.update();
    assert(!state.hasPendingSequencerApply());

    const auto* active =
        core::state::sequencer::sequencerCcLaneView(state.sequencer.pattern);
    const auto* track0 =
        core::state::sequencer::sequencerCcLaneView(state.sequencerTracks.track(0));
    const auto* track5 =
        core::state::sequencer::sequencerCcLaneView(state.sequencerTracks.track(5));
    assert(active != nullptr && active->lanes[0].values[0] == 81);
    assert(track0 != nullptr && track0->lanes[0].values[0] == 81);
    assert(track5 != nullptr && track5->lanes[1].values[7] == 103);

    drainNotifications();
    std::cout
        << "[PASS] test_queued_full_bank_apply_preserves_every_cc_lane_payload\n";
}

void test_new_project_boundary_publishes_runtime_reset_and_clears_activation() {
    CoreStorages storage;
    storage.initAll();
    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);
    core::state::sequencer::SequencerTrackActivationBatch activation;
    assert(state.sequencerTrackActivations.prepare(
        0x0001,
        state.currentSharedTrackEnabledMask(),
        state.sequencerTracks.currentMutedMask(),
        true,
        activation
    ));
    assert(state.sequencerTrackActivations.armPrepared(activation));
    state.sequencerTrackActivations.publishPrepared(activation);
    assert(state.sequencerTrackActivations.pendingTrackMask() == 0x0001);
    const uint32_t before = state.sequencerRuntimeProjectRevision.get();

    state.resetMusicalProject();
    assert(state.sequencerRuntimeProjectRevision.get() != before);
    assert(state.sequencerTrackActivations.pendingTrackMask() == 0);
    assert(state.sequencerTrackActivations.telemetry(0).status ==
           core::state::sequencer::SequencerTrackActivationStatus::IDLE);
    assert(state.sequencerTrackActivations.realtimeView(0).disposition ==
           core::state::sequencer::SequencerTrackActivationRealtimeView::
               Disposition::NORMAL);

    drainNotifications();
    std::cout
        << "[PASS] test_new_project_boundary_publishes_runtime_reset_and_clears_activation\n";
}

void test_sequencer_set_load_merge_preserves_existing_steps() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    // Incoming set snapshot: step 0 and step 3 enabled.
    state.sequencer.pattern.length.set(8);
    state.sequencer.pattern.stepsPerBeat.set(2);
    state.sequencer.pattern.midiChannel.set(1);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(0, 61, 101, 80);
    state.sequencer.pattern.toggle(0);
    state.sequencer.setStepDataAt(3, 65, 99, 70);
    state.sequencer.pattern.toggle(3);
    addGraphContent(state.sequencer.pattern, 9);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::saveSetSlot(state, 4));

    // Live pattern before merge: longer length + existing step 1 enabled.
    state.sequencer.pattern.length.set(16);
    state.sequencer.pattern.stepsPerBeat.set(4);
    state.sequencer.pattern.midiChannel.set(6);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(1, 44, 55, 66);
    state.sequencer.pattern.toggle(1);
    addGraphContent(state.sequencer.pattern, -3);

    const auto status =
        core::state::sequencer::SequencerPersistenceWorkflow::loadSetSlot(state, 4, true);
    assert(status == core::persistence::SlotLoadStatus::OK);

    // Merge keeps current transport config and length, overlays incoming enabled steps only.
    assert(state.sequencer.pattern.length.get() == 16);
    assert(state.sequencer.pattern.stepsPerBeat.get() == 4);
    assert(state.sequencer.pattern.midiChannel.get() == 6);

    assert(state.sequencer.pattern.note[0] == 61);
    assert(state.sequencer.pattern.velocity[0] == 101);
    assert(state.sequencer.pattern.gate[0] == 80);

    // Existing enabled step remains untouched if incoming did not enable it.
    assert(state.sequencer.pattern.note[1] == 44);
    assert(state.sequencer.pattern.velocity[1] == 55);
    assert(state.sequencer.pattern.gate[1] == 66);

    assert(state.sequencer.pattern.note[3] == 65);
    assert(state.sequencer.pattern.velocity[3] == 99);
    assert(state.sequencer.pattern.gate[3] == 70);

    assert(state.sequencer.pattern.enabledMask.get().test(0));
    assert(state.sequencer.pattern.enabledMask.get().test(1));
    assert(state.sequencer.pattern.enabledMask.get().test(3));
    assert(hasGraphContent(state.sequencer.pattern, 9));
    assert(!hasGraphContent(state.sequencer.pattern, -3));

    drainNotifications();

    std::cout << "[PASS] test_sequencer_set_load_merge_preserves_existing_steps\n";
}

void test_sequencer_set_load_merge_is_quantized_when_playing() {
    CoreStorages storage;
    storage.initAll();

    core::state::CoreState state(storage.settings,
                                 storage.macroLibrary,
                                 storage.sequencerPatternLibrary,
                                 storage.sequencerSetLibrary);

    // Prepare incoming set with only step 2 enabled.
    state.sequencer.pattern.length.set(8);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(2, 72, 110, 45);
    state.sequencer.pattern.toggle(2);
    assert(core::state::sequencer::SequencerPersistenceWorkflow::saveSetSlot(state, 6));

    // Live state before queued merge.
    state.sequencer.pattern.length.set(16);
    state.sequencer.pattern.enabledMask.set({});
    state.sequencer.setStepDataAt(1, 48, 64, 55);
    state.sequencer.pattern.toggle(1);
    state.sequencer.playheadStep.set(7);
    state.statusBar.playing.set(true);

    const auto status =
        core::state::sequencer::SequencerPersistenceWorkflow::loadSetSlot(state, 6, true);
    assert(status == core::persistence::SlotLoadStatus::OK);

    // Same-step update must stay deferred.
    state.update();
    assert(state.sequencer.pattern.note[2] != 72 || !state.sequencer.pattern.enabledMask.get().test(2));

    // Next step triggers queued merge.
    state.sequencer.playheadStep.set(8);
    state.update();
    assert(state.sequencer.pattern.length.get() == 16);
    assert(state.sequencer.pattern.note[2] == 72);
    assert(state.sequencer.pattern.velocity[2] == 110);
    assert(state.sequencer.pattern.gate[2] == 45);
    assert(state.sequencer.pattern.enabledMask.get().test(1));
    assert(state.sequencer.pattern.enabledMask.get().test(2));

    drainNotifications();

    std::cout << "[PASS] test_sequencer_set_load_merge_is_quantized_when_playing\n";
}

}  // namespace

int main() {
    std::cout.setf(std::ios::unitbuf);
    std::cout << "==============================================\n";
    std::cout << "CoreState persistence tests\n";
    std::cout << "==============================================\n\n";

    test_project_state_does_not_boot_restore_from_removed_domain_store();
    test_macro_library_roundtrip_and_erase();
    test_macro_library_save_uses_manual_base_without_flush();
    test_macro_config_changes_mark_project_dirty_and_bump_revision();
    test_macro_config_changes_do_not_require_macro_library_storage();
    test_shared_track_pending_save_survives_settings_storage_unavailable();
    test_recovery_from_ram_after_storage_reopen_does_not_reload_stale_card_data();
    test_data_manager_shortcuts_persist_and_sanitize();
    test_data_manager_command_execution_and_slot_probe();
    test_sequencer_library_roundtrip();
    test_sequencer_navigation_context_does_not_boot_restore_without_session();
    test_sequencer_load_is_quantized_to_next_step_when_playing();
    test_sequencer_queued_pattern_load_preserves_graph_content();
    test_direct_load_clears_stale_pending_quantized_apply();
    test_sequencer_save_and_erase_keep_history();
    test_sequencer_load_clears_history_after_apply();
    test_queued_sequencer_load_clears_history_when_applied();
    test_queued_full_bank_apply_preserves_every_cc_lane_payload();
    test_new_project_boundary_publishes_runtime_reset_and_clears_activation();
    test_sequencer_set_load_merge_preserves_existing_steps();
    test_sequencer_set_load_merge_is_quantized_when_playing();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

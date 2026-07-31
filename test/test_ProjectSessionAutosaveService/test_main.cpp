#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>
#include <oc/time/Time.hpp>

#include "../../src/handler/macro/MacroPerformanceDomainServices.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/persistence/ProjectSessionAutosaveService.hpp"
#include "../../src/persistence/ProjectSessionStore.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/modulation/ProjectControlMacroOps.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../support/CoreStorages.hpp"

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;
namespace modulation = core::state::modulation;

uint32_t mockTimeMs() {
    return 0;
}

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           "midi-studio-core-project-session-autosave-test";
}

void resetTestRoot() {
    std::error_code ec;
    std::filesystem::remove_all(testRoot(), ec);
}

core::state::CoreState makeCoreState(test_support::CoreStorages& storages) {
    return core::state::CoreState{
        storages.settings,
    };
}

void configureProject(core::state::CoreState& state, const char* id, uint8_t note) {
    state.project.metadata.id.fill('\0');
    std::strncpy(state.project.metadata.id.data(), id, state.project.metadata.id.size() - 1U);
    state.project.metadata.name.fill('\0');
    std::strncpy(state.project.metadata.name.data(), id, state.project.metadata.name.size() - 1U);
    state.project.metadata.hasSavedIdentity = true;

    auto& page = state.pages.activePageData();
    page.cc[0] = 74;
    page.values[0] = 0.65f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.setContentLength(8);
    state.sequencer.setStepDataAt(0, note, 100, 75);
    state.sequencer.pattern.toggle(0);
}

core::state::modulation::ProjectModulationResult beginLfoAudition(
    core::state::CoreState& state,
    uint8_t macro = 1U
) {
    const auto address = core::state::macro::MacroAutomationSlotAddress{
        .track = state.pages.currentActiveTrack(),
        .page = state.pages.currentActivePage(),
        .macro = macro,
    };
    modulation::ModulatorLfoDraft source{};
    source.name = "Autosave guard";
    source.parameters.periodTicks = modulation::PROJECT_CONTROL_TICKS_PER_BEAT;
    source.parameters.shape = modulation::ModulatorLfoShape::SINE;
    source.parameters.retrigger = modulation::ModulatorRetriggerPolicy::TRANSPORT;
    source.parameters.timing = modulation::ModulatorTimingMode::SYNC;

    modulation::ModulationBindingDraft binding{};
    binding.destination = modulation::projectControlDestination(address);
    binding.amountQ15 = 8192;
    binding.application = modulation::ModulationApplication::NATURAL;
    return state.macroHistory.beginLfoModulatorAudition(
        state.pages,
        address,
        source,
        binding
    );
}

core::persistence::ProductFileService makeProductFiles(oc::impl::HostFileSystem& filesystem) {
    core::persistence::ProductFileService files(filesystem);
    assert(files.init());
    return files;
}

void assertNoCurrentSessionFile() {
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "session" / "current.mspj"
    ));
}

void assertCurrentSessionNote(core::persistence::ProductFileService& files, uint8_t note) {
    core::persistence::ProjectSessionStore store(files);
    project::ProjectSnapshot loaded;
    project_file::LoadReport report{};
    auto result = store.loadCurrent(loaded, &report);
    assert(result);
    assert(report.ok());

    test_support::CoreStorages storages;
    auto restored = makeCoreState(storages);
    assert(project::applyProjectSnapshot(restored, loaded));
    assert(restored.sequencer.pattern.note[0] == note);
    assert(restored.sequencer.pattern.isEnabled(0));
}

core::persistence::ProjectSessionAutosaveService::Result updateUntilSettled(
    core::persistence::ProjectSessionAutosaveService& autosave,
    core::state::CoreState& state,
    uint32_t nowMs
) {
    for (uint16_t step = 0; step < 192; ++step) {
        auto result = autosave.update(state, nowMs);
        if (result.status !=
            core::persistence::ProjectSessionAutosaveService::Status::SAVING) {
            return result;
        }
    }
    assert(false && "autosave did not settle");
    return {};
}

void test_waits_until_delay_before_saving() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 1000);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p002", 61);
    state.markProjectMutated();

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    auto waiting = autosave.update(state, requestedAt + 999U);
    assert(waiting.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    auto started = autosave.update(state, requestedAt + 1000U);
    assert(started.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    assertNoCurrentSessionFile();

    auto saved = updateUntilSettled(autosave, state, requestedAt + 1000U);
    assert(saved.saved());
    assert(saved.bytes > 0);
    assert(!state.hasPendingProjectSessionSave());
    assertCurrentSessionNote(files, 61);

    std::cout << "[PASS] test_waits_until_delay_before_saving\n";
}

void test_coalesces_until_latest_request_timestamp() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 1000);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p003", 62);
    state.markProjectMutated();
    const uint32_t firstRequestAt = state.projectSessionSaveTimestampMs();

    state.sequencer.setStepDataAt(0, 69, 100, 75);
    state.markProjectMutated();
    const uint32_t secondRequestAt = state.projectSessionSaveTimestampMs();

    auto stillWaiting = autosave.update(state, secondRequestAt + 999U);
    assert(stillWaiting.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    auto saved = updateUntilSettled(autosave, state, secondRequestAt + 1000U);
    assert(saved.saved());
    assert(saved.modifiedCounter == state.project.metadata.modifiedCounter);
    assert(saved.modifiedCounter >= 2U);
    assert(secondRequestAt >= firstRequestAt);
    assertCurrentSessionNote(files, 69);

    std::cout << "[PASS] test_coalesces_until_latest_request_timestamp\n";
}

void test_write_blocked_keeps_pending_session() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p004", 63);
    state.markProjectMutated();

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    auto blocked = autosave.update(state, requestedAt + 100U, true);
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    auto saved = updateUntilSettled(autosave, state, requestedAt + 101U);
    assert(saved.saved());
    assertCurrentSessionNote(files, 63);

    std::cout << "[PASS] test_write_blocked_keeps_pending_session\n";
}

void test_pending_live_edit_blocks_session_save_until_flushed() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    state.markProjectMutated();
    const auto macros = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    macros.setManualValue(0, 0.75f);

    assert(state.hasPendingProjectMutationCoalescing());
    const uint32_t firstRequestAt = state.projectSessionSaveTimestampMs();
    auto blocked = autosave.update(
        state,
        firstRequestAt + 100U,
        state.hasPendingProjectMutationCoalescing()
    );
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    state.flushProjectMutationCoalescing();
    assert(!state.hasPendingProjectMutationCoalescing());
    const uint32_t flushedRequestAt = state.projectSessionSaveTimestampMs();
    auto saved = updateUntilSettled(autosave, state, flushedRequestAt + 100U);
    assert(saved.saved());
    assert(!state.hasPendingProjectSessionSave());

    project::ProjectSnapshot loaded;
    project_file::LoadReport report{};
    assert(store.loadCurrent(loaded, &report));
    const auto& activeTrack = loaded.macroTracks[loaded.sharedTrackActive];
    assert(activeTrack.pages[activeTrack.activePage].values[0] == 0.75f);

    std::cout << "[PASS] test_pending_live_edit_blocks_session_save_until_flushed\n";
}

void test_mutation_during_capture_restarts_from_latest_revision() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p006", 65);
    state.markProjectMutated();

    const uint32_t firstRequestAt = state.projectSessionSaveTimestampMs();
    auto started = autosave.update(state, firstRequestAt + 100U);
    assert(started.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);

    state.sequencer.setStepDataAt(0, 77, 100, 75);
    state.markProjectMutated();
    const uint32_t latestRequestAt = state.projectSessionSaveTimestampMs();

    auto stale = autosave.update(state, latestRequestAt + 99U);
    assert(stale.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assertNoCurrentSessionFile();

    auto saved = updateUntilSettled(autosave, state, latestRequestAt + 100U);
    assert(saved.saved());
    assert(saved.modifiedCounter == state.project.metadata.modifiedCounter);
    assertCurrentSessionNote(files, 77);

    std::cout << "[PASS] test_mutation_during_capture_restarts_from_latest_revision\n";
}

void test_write_block_pauses_an_in_flight_capture() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p007", 66);
    state.markProjectMutated();

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    auto started = autosave.update(state, requestedAt + 100U);
    assert(started.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);

    auto blocked = autosave.update(state, requestedAt + 101U, true);
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assertNoCurrentSessionFile();

    auto saved = updateUntilSettled(autosave, state, requestedAt + 102U);
    assert(saved.saved());
    assertCurrentSessionNote(files, 66);

    std::cout << "[PASS] test_write_block_pauses_an_in_flight_capture\n";
}

void test_mutation_after_tmp_write_discards_the_stale_transaction() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p008", 67);
    state.markProjectMutated();

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    for (uint8_t slice = 0; slice < 7; ++slice) {
        const auto progress = autosave.update(state, requestedAt + 100U);
        assert(progress.status ==
               core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    }

    const auto tmpPath =
        testRoot() / "midi-studio" / "tmp" / "session.current.tmp";
    assert(std::filesystem::is_regular_file(tmpPath));
    assertNoCurrentSessionFile();
    assert(autosave.writeSessionActive());
    assert(files.writeSessionActive());

    state.sequencer.setStepDataAt(0, 78, 100, 75);
    state.markProjectMutated();
    const uint32_t latestRequestAt = state.projectSessionSaveTimestampMs();

    const auto waiting = autosave.update(state, latestRequestAt + 99U);
    assert(waiting.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(!std::filesystem::exists(tmpPath));
    assertNoCurrentSessionFile();
    assert(!autosave.writeSessionActive());
    assert(!files.writeSessionActive());

    const auto saved = updateUntilSettled(autosave, state, latestRequestAt + 100U);
    assert(saved.saved());
    assertCurrentSessionNote(files, 78);

    std::cout << "[PASS] test_mutation_after_tmp_write_discards_the_stale_transaction\n";
}

void test_pending_live_edit_aborts_an_in_flight_write() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p010", 70);
    state.markProjectMutated();

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    for (uint8_t slice = 0; slice < 7; ++slice) {
        const auto progress = autosave.update(state, requestedAt + 100U);
        assert(progress.status ==
               core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    }

    const auto tmpPath =
        testRoot() / "midi-studio" / "tmp" / "session.current.tmp";
    assert(std::filesystem::is_regular_file(tmpPath));
    assert(autosave.writeSessionActive());
    assert(files.writeSessionActive());

    const auto macros = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    macros.setManualValue(0, 0.8f);
    assert(state.hasPendingProjectMutationCoalescing());

    const auto blocked = autosave.update(
        state,
        requestedAt + 101U,
        state.hasPendingProjectMutationCoalescing()
    );
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assert(!std::filesystem::exists(tmpPath));
    assert(!autosave.writeSessionActive());
    assert(!files.writeSessionActive());
    assert(state.hasPendingProjectSessionSave());

    state.flushProjectMutationCoalescing();
    const uint32_t latestRequestAt = state.projectSessionSaveTimestampMs();
    const auto saved = updateUntilSettled(autosave, state, latestRequestAt + 100U);
    assert(saved.saved());

    project::ProjectSnapshot loaded;
    project_file::LoadReport report{};
    assert(store.loadCurrent(loaded, &report));
    const auto& activeTrack = loaded.macroTracks[loaded.sharedTrackActive];
    assert(activeTrack.pages[activeTrack.activePage].values[0] == 0.8f);

    std::cout << "[PASS] test_pending_live_edit_aborts_an_in_flight_write\n";
}

void test_modulator_audition_blocks_and_aborts_an_in_flight_autosave() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p011", 71);
    state.markProjectMutated();

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    const auto started = autosave.update(state, requestedAt + 100U);
    assert(started.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);

    const auto begun = beginLfoAudition(state);
    assert(begun.changed());
    assert(state.hasPendingProjectTransaction());

    const auto blocked = autosave.update(state, requestedAt + 101U);
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();
    assert(!autosave.writeSessionActive());
    assert(!files.writeSessionActive());

    assert(state.macroHistory.abortPendingModulatorAudition(state.pages));
    assert(!state.hasPendingProjectTransaction());
    const auto saved = updateUntilSettled(autosave, state, requestedAt + 102U);
    assert(saved.saved());
    assertCurrentSessionNote(files, 71);

    project::ProjectSnapshot loaded;
    project_file::LoadReport report{};
    assert(store.loadCurrent(loaded, &report));
    assert(loaded.projectControl);
    assert(loaded.projectControl->modulation.sourceCount == 0U);
    assert(loaded.projectControl->modulation.outputBindingCount == 0U);

    std::cout
        << "[PASS] modulator audition blocks and aborts in-flight autosave\n";
}

void test_flush_refuses_an_active_modulator_audition() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p012", 72);
    state.markProjectMutated();
    assert(beginLfoAudition(state).changed());

    const auto blocked = autosave.flush(state);
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    assert(state.macroHistory.abortPendingModulatorAudition(state.pages));
    const auto saved = autosave.flush(state);
    assert(saved.saved());
    assertCurrentSessionNote(files, 72);

    std::cout << "[PASS] flush refuses an active modulator audition\n";
}

void test_flush_writes_without_waiting() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 5000);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p005", 64);
    state.markProjectMutated();

    auto saved = autosave.flush(state);
    assert(saved.saved());
    assert(!state.hasPendingProjectSessionSave());
    assertCurrentSessionNote(files, 64);

    std::cout << "[PASS] test_flush_writes_without_waiting\n";
}

void test_destruction_cancels_an_in_flight_write() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p009", 68);
    state.markProjectMutated();

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    {
        core::persistence::ProjectSessionAutosaveService autosave(store, 100);
        for (uint8_t slice = 0; slice < 7; ++slice) {
            const auto progress = autosave.update(state, requestedAt + 100U);
            assert(progress.status ==
                   core::persistence::ProjectSessionAutosaveService::Status::SAVING);
        }
        assert(autosave.writeSessionActive());
        assert(files.writeSessionActive());
    }

    assert(!store.saveCurrentInProgress());
    assert(!files.writeSessionActive());
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "session.current.tmp"
    ));
    assertNoCurrentSessionFile();

    std::cout << "[PASS] test_destruction_cancels_an_in_flight_write\n";
}

}  // namespace

int main() {
    oc::time::setProvider(mockTimeMs);
    std::cout << "====================================================\n";
    std::cout << "ProjectSessionAutosaveService tests\n";
    std::cout << "====================================================\n\n";

    test_waits_until_delay_before_saving();
    test_coalesces_until_latest_request_timestamp();
    test_write_blocked_keeps_pending_session();
    test_pending_live_edit_blocks_session_save_until_flushed();
    test_mutation_during_capture_restarts_from_latest_revision();
    test_write_block_pauses_an_in_flight_capture();
    test_mutation_after_tmp_write_discards_the_stale_transaction();
    test_pending_live_edit_aborts_an_in_flight_write();
    test_modulator_audition_blocks_and_aborts_an_in_flight_autosave();
    test_flush_refuses_an_active_modulator_audition();
    test_flush_writes_without_waiting();
    test_destruction_cancels_an_in_flight_write();

    resetTestRoot();

    std::cout << "\n====================================================\n";
    std::cout << "All tests passed\n";
    std::cout << "====================================================\n";
    return 0;
}

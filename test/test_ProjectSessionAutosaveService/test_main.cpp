#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>

#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/persistence/ProjectSessionAutosaveService.hpp"
#include "../../src/persistence/ProjectSessionStore.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../support/CoreStorages.hpp"

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;

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
        storages.macroWorkspace,
        storages.macroLibrary,
        storages.sequencerWorkspace,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary,
    };
}

void configureProject(core::state::CoreState& state, const char* id, uint8_t note) {
    state.project.metadata.id.fill('\0');
    std::strncpy(state.project.metadata.id.data(), id, state.project.metadata.id.size() - 1U);
    state.project.metadata.hasSavedIdentity = true;

    auto& page = state.pages.activePageData();
    page.cc[0] = 74;
    page.values[0] = 0.65f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.length.set(8);
    state.sequencer.setStepDataAt(0, note, 100, 75);
    state.sequencer.pattern.toggle(0);
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
    assert(result.value().loadStatus == project_file::LoadStatus::OK);
    assert(report.ok());

    test_support::CoreStorages storages;
    auto restored = makeCoreState(storages);
    assert(project::applyProjectSnapshot(restored, loaded));
    assert(restored.sequencer.pattern.note[0] == note);
    assert(restored.sequencer.pattern.isEnabled(0));
}

void test_waits_until_delay_before_saving() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionAutosaveService autosave(files, 1000);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "P002", 61);
    state.markProjectMutated();

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    auto waiting = autosave.update(state, requestedAt + 999U);
    assert(waiting.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    auto saved = autosave.update(state, requestedAt + 1000U);
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
    core::persistence::ProjectSessionAutosaveService autosave(files, 1000);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "P003", 62);
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

    auto saved = autosave.update(state, secondRequestAt + 1000U);
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
    core::persistence::ProjectSessionAutosaveService autosave(files, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "P004", 63);
    state.markProjectMutated();

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    auto blocked = autosave.update(state, requestedAt + 100U, true);
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    auto saved = autosave.update(state, requestedAt + 101U, false);
    assert(saved.saved());
    assertCurrentSessionNote(files, 63);

    std::cout << "[PASS] test_write_blocked_keeps_pending_session\n";
}

void test_flush_writes_without_waiting() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionAutosaveService autosave(files, 5000);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "P005", 64);
    state.markProjectMutated();

    auto saved = autosave.flush(state);
    assert(saved.saved());
    assert(!state.hasPendingProjectSessionSave());
    assertCurrentSessionNote(files, 64);

    std::cout << "[PASS] test_flush_writes_without_waiting\n";
}

}  // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << "ProjectSessionAutosaveService tests\n";
    std::cout << "====================================================\n\n";

    test_waits_until_delay_before_saving();
    test_coalesces_until_latest_request_timestamp();
    test_write_blocked_keeps_pending_session();
    test_flush_writes_without_waiting();

    resetTestRoot();

    std::cout << "\n====================================================\n";
    std::cout << "All tests passed\n";
    std::cout << "====================================================\n";
    return 0;
}

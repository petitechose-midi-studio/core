#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>

#include "../../src/persistence/ProjectSessionStore.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../support/CoreStorages.hpp"

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() / "midi-studio-core-project-session-store-test";
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

void configureSession(core::state::CoreState& state,
                      const char* projectId,
                      const char* name,
                      uint32_t modifiedCounter,
                      float macroValue,
                      uint8_t sequencerNote) {
    state.project.metadata.id.fill('\0');
    std::strncpy(state.project.metadata.id.data(), projectId, state.project.metadata.id.size() - 1U);
    state.project.metadata.name.fill('\0');
    std::strncpy(state.project.metadata.name.data(), projectId, state.project.metadata.name.size() - 1U);
    state.project.metadata.modifiedCounter = modifiedCounter;
    state.project.metadata.hasSavedIdentity = true;
    state.project.metadata.dirty = true;

    state.statusBar.tempo.set(120.0f + static_cast<float>(modifiedCounter));
    state.statusBar.tempoDisplay.set(state.statusBar.tempo.get());

    auto& page = state.pages.activePageData();
    std::strncpy(page.name, name, sizeof(page.name) - 1U);
    page.cc[0] = static_cast<uint8_t>(40U + modifiedCounter);
    page.values[0] = macroValue;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.setContentLength(12);
    state.sequencer.setStepDataAt(0, sequencerNote, 110, 75);
    state.sequencer.pattern.toggle(0);
    state.sequencer.focusedStep.set(0);
}

project::ProjectSnapshot capture(core::state::CoreState& state) {
    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));
    return snapshot;
}

core::persistence::ProjectSessionStore makeStore(core::persistence::ProductFileService& files) {
    assert(files.init());
    return core::persistence::ProjectSessionStore(files);
}

void assertLoadedSession(core::persistence::ProjectSessionStore& store,
                         const char* expectedId,
                         const char* expectedName,
                         uint32_t expectedCounter,
                         uint8_t expectedNote) {
    project::ProjectSnapshot loaded;
    project_file::LoadReport report{};
    auto loadedResult = store.loadCurrent(loaded, &report);
    assert(loadedResult);
    assert(loadedResult.value().loadStatus == project_file::LoadStatus::OK);
    assert(loadedResult.value().overwriteSafe);
    assert(report.ok());

    test_support::CoreStorages storages;
    auto runtime = makeCoreState(storages);
    assert(project::applyProjectSnapshot(runtime, loaded));

    assert(std::strcmp(runtime.project.metadata.id.data(), expectedId) == 0);
    assert(std::strcmp(runtime.project.metadata.name.data(), expectedId) == 0);
    assert(runtime.project.metadata.modifiedCounter == expectedCounter);
    assert(runtime.project.metadata.dirty);
    assert(std::strcmp(runtime.pages.activePageData().name, expectedName) == 0);
    assert(runtime.pages.activePageData().cc[0] == 40U + expectedCounter);
    assert(runtime.sequencer.pattern.note[0] == expectedNote);
    assert(runtime.sequencer.pattern.isEnabled(0));
}

void test_current_session_roundtrip_uses_session_path() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    auto store = makeStore(files);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureSession(state, "p003", "Current", 3, 0.42f, 67);

    auto saved = store.saveCurrent(capture(state));
    assert(saved);
    assert(saved.value().bytesWritten > 0);
    assert(std::strcmp(saved.value().projectPath, "session/current.mspj") == 0);
    assert(std::filesystem::is_regular_file(
        testRoot() / "midi-studio" / "session" / "current.mspj"
    ));
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "p003.mspj"
    ));

    assertLoadedSession(store, "p003", "Current", 3, 67);

    std::cout << "[PASS] test_current_session_roundtrip_uses_session_path\n";
}

void test_current_session_overwrite_uses_backup_commit() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    auto store = makeStore(files);

    test_support::CoreStorages storages;
    auto first = makeCoreState(storages);
    configureSession(first, "p001", "First", 1, 0.21f, 61);
    assert(store.saveCurrent(capture(first)));

    auto second = makeCoreState(storages);
    configureSession(second, "p002", "Second", 2, 0.72f, 72);
    assert(store.saveCurrent(capture(second)));

    assertLoadedSession(store, "p002", "Second", 2, 72);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "session" / "current.bak"
    ));

    std::cout << "[PASS] test_current_session_overwrite_uses_backup_commit\n";
}

void test_stale_current_session_tmp_is_replaced() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    auto store = makeStore(files);

    const uint8_t stale[] = {'s', 't', 'a', 'l', 'e'};
    assert(files.write("tmp/session.current.tmp", 0, stale, sizeof(stale)));

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureSession(state, "p004", "TmpClean", 4, 0.55f, 74);
    assert(store.saveCurrent(capture(state)));

    assertLoadedSession(store, "p004", "TmpClean", 4, 74);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "session.current.tmp"
    ));

    std::cout << "[PASS] test_stale_current_session_tmp_is_replaced\n";
}

void test_current_session_load_recovers_interrupted_backup_commit() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    auto store = makeStore(files);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureSession(state, "p005", "Backup", 5, 0.64f, 76);
    assert(store.saveCurrent(capture(state)));
    assert(files.rename("session/current.mspj", "session/current.bak"));

    assertLoadedSession(store, "p005", "Backup", 5, 76);
    assert(std::filesystem::is_regular_file(
        testRoot() / "midi-studio" / "session" / "current.mspj"
    ));
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "session" / "current.bak"
    ));

    std::cout << "[PASS] test_current_session_load_recovers_interrupted_backup_commit\n";
}

void test_corrupt_current_session_reports_storage_corrupt() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    auto store = makeStore(files);

    const uint8_t corrupt[] = {'n', 'o', 't', 'm', 's', 'p', 'j'};
    assert(files.write("session/current.mspj", 0, corrupt, sizeof(corrupt)));

    project::ProjectSnapshot loaded;
    auto result = store.loadCurrent(loaded);
    assert(!result);
    assert(result.error().code == oc::type::ErrorCode::STORAGE_CORRUPT);

    std::cout << "[PASS] test_corrupt_current_session_reports_storage_corrupt\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectSessionStore tests\n";
    std::cout << "==============================================\n\n";

    test_current_session_roundtrip_uses_session_path();
    test_current_session_overwrite_uses_backup_commit();
    test_stale_current_session_tmp_is_replaced();
    test_current_session_load_recovers_interrupted_backup_commit();
    test_corrupt_current_session_reports_storage_corrupt();

    resetTestRoot();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

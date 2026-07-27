#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>

#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/persistence/ProjectSessionRestoreService.hpp"
#include "../../src/persistence/ProjectSessionStore.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../support/CoreStorages.hpp"

namespace {

namespace project = core::state::project;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           "midi-studio-core-project-session-restore-test";
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

void configureSession(core::state::CoreState& state, const char* id, uint8_t note) {
    state.project.metadata.id.fill('\0');
    std::strncpy(state.project.metadata.id.data(), id, state.project.metadata.id.size() - 1U);
    state.project.metadata.name.fill('\0');
    std::strncpy(state.project.metadata.name.data(), id, state.project.metadata.name.size() - 1U);
    state.project.metadata.hasSavedIdentity = true;
    state.project.metadata.modifiedCounter = 9;
    state.project.metadata.dirty = true;

    state.statusBar.tempo.set(141.0f);
    state.statusBar.tempoDisplay.set(141.0f);

    auto& page = state.pages.activePageData();
    std::strncpy(page.name, "Restored", sizeof(page.name) - 1U);
    page.cc[0] = 71;
    page.values[0] = 0.33f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.setContentLength(10);
    state.sequencer.setStepDataAt(0, note, 111, 80);
    state.sequencer.pattern.toggle(0);
    state.sequencer.focusedStep.set(0);
}

project::ProjectSnapshot capture(core::state::CoreState& state) {
    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));
    return snapshot;
}

void test_missing_session_is_non_fatal() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    assert(files.init());
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionRestoreService restore(store);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);

    auto result = restore.restore(state);
    assert(result.status == core::persistence::ProjectSessionRestoreService::Status::MISSING);
    assert(!result.restored());
    assert(state.project.metadata.id[0] == '\0');
    assert(std::strcmp(state.project.metadata.name.data(), "untitled") == 0);
    assert(!state.project.metadata.hasSavedIdentity);
    assert(!state.hasPendingProjectSessionSave());

    std::cout << "[PASS] test_missing_session_is_non_fatal\n";
}

void test_valid_session_restores_runtime_project() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    assert(files.init());

    test_support::CoreStorages sourceStorages;
    auto source = makeCoreState(sourceStorages);
    configureSession(source, "p009", 70);
    core::persistence::ProjectSessionStore store(files);
    assert(store.saveCurrent(capture(source)));

    test_support::CoreStorages runtimeStorages;
    auto runtime = makeCoreState(runtimeStorages);
    assert(runtime.project.metadata.id[0] == '\0');
    assert(std::strcmp(runtime.project.metadata.name.data(), "untitled") == 0);

    core::persistence::ProjectSessionRestoreService restore(store);
    auto result = restore.restore(runtime);
    assert(result.restored());
    assert(result.bytes > 0);

    assert(std::strcmp(runtime.project.metadata.id.data(), "p009") == 0);
    assert(std::strcmp(runtime.project.metadata.name.data(), "p009") == 0);
    assert(runtime.project.metadata.modifiedCounter == 9);
    assert(runtime.project.metadata.dirty);
    assert(runtime.statusBar.tempo.get() == 141.0f);
    assert(std::strcmp(runtime.pages.activePageData().name, "Restored") == 0);
    assert(runtime.pages.activePageData().cc[0] == 71);
    assert(runtime.macros.slots[0].value.get() == 0.33f);
    assert(runtime.sequencer.pattern.length.get() == 10);
    assert(runtime.sequencer.pattern.isEnabled(0));
    assert(runtime.sequencer.pattern.note[0] == 70);
    assert(!runtime.hasPendingProjectSessionSave());

    std::cout << "[PASS] test_valid_session_restores_runtime_project\n";
}

void test_corrupt_session_reports_degraded_and_keeps_runtime() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    assert(files.init());
    const uint8_t corrupt[] = {'b', 'a', 'd'};
    assert(files.write("session/current.mspj", 0, corrupt, sizeof(corrupt)));
    core::persistence::ProjectSessionStore store(files);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    state.statusBar.tempo.set(123.0f);

    core::persistence::ProjectSessionRestoreService restore(store);
    auto result = restore.restore(state);
    assert(result.status == core::persistence::ProjectSessionRestoreService::Status::DEGRADED);
    assert(!result.restored());
    assert(state.project.metadata.id[0] == '\0');
    assert(std::strcmp(state.project.metadata.name.data(), "untitled") == 0);
    assert(state.statusBar.tempo.get() == 123.0f);
    assert(!state.hasPendingProjectSessionSave());

    std::cout << "[PASS] test_corrupt_session_reports_degraded_and_keeps_runtime\n";
}

}  // namespace

int main() {
    std::cout << "====================================================\n";
    std::cout << "ProjectSessionRestoreService tests\n";
    std::cout << "====================================================\n\n";

    test_missing_session_is_non_fatal();
    test_valid_session_restores_runtime_project();
    test_corrupt_session_reports_degraded_and_keeps_runtime();

    resetTestRoot();

    std::cout << "\n====================================================\n";
    std::cout << "All tests passed\n";
    std::cout << "====================================================\n";
    return 0;
}

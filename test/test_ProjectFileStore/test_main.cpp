#include <cassert>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/persistence/ProjectFileStore.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../support/CoreStorages.hpp"

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() / "midi-studio-core-project-file-store-test";
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

struct CurrentStatFailureFileSystem : oc::interface::IFileSystem {
    explicit CurrentStatFailureFileSystem(const char* rootPath)
        : delegate(rootPath) {}

    oc::type::Result<void> init() override { return delegate.init(); }
    bool available() const override { return delegate.available(); }
    oc::type::Result<oc::interface::FileInfo> stat(const char* path) override {
        if (failCurrentStat && path &&
            std::strcmp(path, "/midi-studio/projects/P321/project.mspj") == 0) {
            return oc::type::Result<oc::interface::FileInfo>::err(
                {oc::type::ErrorCode::STORAGE_READ_FAILED, "forced current stat failure"}
            );
        }
        return delegate.stat(path);
    }
    oc::type::Result<void> list(
        const char* path,
        oc::interface::DirectoryEntryVisitor visitor,
        void* context
    ) override {
        return delegate.list(path, visitor, context);
    }
    oc::type::Result<void> createDirectory(const char* path) override {
        return delegate.createDirectory(path);
    }
    oc::type::Result<void> remove(
        const char* path,
        oc::interface::RemoveMode mode = oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
    ) override {
        return delegate.remove(path, mode);
    }
    oc::type::Result<void> rename(const char* fromPath, const char* toPath) override {
        return delegate.rename(fromPath, toPath);
    }
    oc::type::Result<size_t> read(
        const char* path,
        uint32_t offset,
        uint8_t* buffer,
        size_t size
    ) override {
        return delegate.read(path, offset, buffer, size);
    }
    oc::type::Result<size_t> write(
        const char* path,
        uint32_t offset,
        const uint8_t* data,
        size_t size
    ) override {
        return delegate.write(path, offset, data, size);
    }
    oc::type::Result<void> flush(const char* path) override {
        return delegate.flush(path);
    }
    oc::type::Result<void> beginWrite(const char* path, uint32_t expectedSize) override {
        return delegate.beginWrite(path, expectedSize);
    }
    oc::type::Result<size_t> appendWrite(const uint8_t* data, size_t size) override {
        return delegate.appendWrite(data, size);
    }
    oc::type::Result<void> finishWrite() override {
        return delegate.finishWrite();
    }
    void abortWrite() override {
        delegate.abortWrite();
    }

    oc::impl::HostFileSystem delegate;
    bool failCurrentStat = false;
};

void configureProject(core::state::CoreState& state, const char* name, uint32_t modifiedCounter) {
    std::strncpy(state.project.metadata.id.data(), "P321", state.project.metadata.id.size() - 1U);
    std::strncpy(state.project.metadata.name.data(), name, state.project.metadata.name.size() - 1U);
    state.project.metadata.modifiedCounter = modifiedCounter;
    state.project.metadata.hasSavedIdentity = true;
    state.project.metadata.dirty = false;

    state.statusBar.tempo.set(111.0f + static_cast<float>(modifiedCounter));
    state.statusBar.tempoDisplay.set(state.statusBar.tempo.get());

    auto& page = state.pages.activePageData();
    std::strncpy(page.name, name, sizeof(page.name) - 1U);
    page.cc[1] = static_cast<uint8_t>(70U + modifiedCounter);
    page.values[1] = 0.25f + static_cast<float>(modifiedCounter) * 0.01f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.length.set(8);
    state.sequencer.pattern.stepsPerBeat.set(4);
    state.sequencer.setStepDataAt(1, static_cast<uint8_t>(60U + modifiedCounter), 100, 80);
    state.sequencer.pattern.toggle(1);
    state.sequencer.focusedStep.set(1);
}

project::ProjectSnapshot capture(core::state::CoreState& state) {
    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));
    return snapshot;
}

core::persistence::ProjectFileStore makeStore(core::persistence::ProductFileService& files) {
    assert(files.init());
    return core::persistence::ProjectFileStore(files);
}

void assertLoadedProject(core::persistence::ProjectFileStore& store,
                         const char* expectedName,
                         uint32_t expectedCounter) {
    project::ProjectSnapshot loaded;
    project_file::LoadReport report{};
    auto loadedResult = store.load("P321", loaded, &report);
    assert(loadedResult);
    assert(loadedResult.value().loadStatus == project_file::LoadStatus::OK);
    assert(loadedResult.value().overwriteSafe);
    assert(report.ok());

    test_support::CoreStorages storages;
    auto runtime = makeCoreState(storages);
    assert(project::applyProjectSnapshot(runtime, loaded));

    assert(std::strcmp(runtime.project.metadata.id.data(), "P321") == 0);
    assert(std::strcmp(runtime.project.metadata.name.data(), expectedName) == 0);
    assert(runtime.project.metadata.modifiedCounter == expectedCounter);
    assert(std::strcmp(runtime.pages.activePageData().name, expectedName) == 0);
    assert(runtime.pages.activePageData().cc[1] == 70U + expectedCounter);
    assert(runtime.sequencer.pattern.note[1] == 60U + expectedCounter);
    assert(runtime.sequencer.pattern.isEnabled(1));
}

void test_save_load_project_snapshot_roundtrip() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    auto store = makeStore(files);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "ProjectFS", 1);
    auto snapshot = capture(state);

    auto saved = store.save(snapshot);
    assert(saved);
    assert(saved.value().bytesWritten > 0);
    assert(std::strcmp(saved.value().projectPath, "projects/P321/project.mspj") == 0);
    assert(std::filesystem::is_regular_file(
        testRoot() / "midi-studio" / "projects" / "P321" / "project.mspj"
    ));
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "P321.project.tmp"
    ));

    assertLoadedProject(store, "ProjectFS", 1);

    std::cout << "[PASS] test_save_load_project_snapshot_roundtrip\n";
}

void test_save_overwrites_existing_project_through_backup_commit() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    auto store = makeStore(files);

    test_support::CoreStorages storages;
    auto first = makeCoreState(storages);
    configureProject(first, "First", 1);
    assert(store.save(capture(first)));

    auto second = makeCoreState(storages);
    configureProject(second, "Second", 2);
    assert(store.save(capture(second)));

    assertLoadedProject(store, "Second", 2);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "P321" / "project.bak"
    ));

    std::cout << "[PASS] test_save_overwrites_existing_project_through_backup_commit\n";
}

void test_stale_tmp_is_replaced_on_save() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    auto store = makeStore(files);

    const uint8_t stale[] = {'s', 't', 'a', 'l', 'e'};
    assert(files.write("tmp/P321.project.tmp", 0, stale, sizeof(stale)));

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "TmpClean", 3);
    assert(store.save(capture(state)));

    assertLoadedProject(store, "TmpClean", 3);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "P321.project.tmp"
    ));

    std::cout << "[PASS] test_stale_tmp_is_replaced_on_save\n";
}

void test_save_propagates_current_stat_error_before_commit() {
    resetTestRoot();

    CurrentStatFailureFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    auto store = makeStore(files);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "StatFail", 4);

    filesystem.failCurrentStat = true;
    auto saved = store.save(capture(state));
    assert(!saved);
    assert(saved.error().code == oc::type::ErrorCode::STORAGE_READ_FAILED);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "P321" / "project.mspj"
    ));

    std::cout << "[PASS] test_save_propagates_current_stat_error_before_commit\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectFileStore tests\n";
    std::cout << "==============================================\n\n";

    test_save_load_project_snapshot_roundtrip();
    test_save_overwrites_existing_project_through_backup_commit();
    test_stale_tmp_is_replaced_on_save();
    test_save_propagates_current_stat_error_before_commit();

    resetTestRoot();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

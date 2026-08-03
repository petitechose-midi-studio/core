#include <cassert>
#include <array>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>

#include <oc/impl/HostFileSystem.hpp>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/persistence/ProjectFileStore.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../../src/state/project/ProjectSlug.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/ProductFileTestMutation.hpp"

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
    };
}

std::array<char, project::PROJECT_SLUG_SIZE> maxLengthSlug() {
    std::array<char, project::PROJECT_SLUG_SIZE> slug{};
    for (size_t i = 0; i < project::PROJECT_SLUG_MAX_LENGTH; ++i) {
        slug[i] = 'a';
    }
    slug[project::PROJECT_SLUG_MAX_LENGTH] = '\0';
    return slug;
}

struct CurrentStatFailureFileSystem : oc::interface::IFileSystem {
    explicit CurrentStatFailureFileSystem(const char* rootPath)
        : delegate(rootPath) {}

    oc::type::Result<void> init() override { return delegate.init(); }
    bool available() const override { return delegate.available(); }
    oc::type::Result<oc::interface::FileInfo> stat(const char* path) override {
        if (failCurrentStat && path &&
            std::strcmp(path, "/midi-studio/projects/p321.mspj") == 0) {
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

void configureProject(core::state::CoreState& state,
                      const char* pageName,
                      uint32_t modifiedCounter,
                      const char* projectId = "p321") {
    state.project.metadata.id.fill('\0');
    std::strncpy(state.project.metadata.id.data(), projectId, state.project.metadata.id.size() - 1U);
    state.project.metadata.name.fill('\0');
    std::strncpy(state.project.metadata.name.data(), projectId, state.project.metadata.name.size() - 1U);
    state.project.metadata.modifiedCounter = modifiedCounter;
    state.project.metadata.hasSavedIdentity = true;
    state.project.metadata.dirty = false;

    state.statusBar.tempo.set(111.0f + static_cast<float>(modifiedCounter));
    state.statusBar.tempoDisplay.set(state.statusBar.tempo.get());

    auto& page = state.pages.activePageData();
    std::strncpy(page.name, pageName, sizeof(page.name) - 1U);
    page.cc[1] = static_cast<uint8_t>(70U + modifiedCounter);
    page.values[1] = 0.25f + static_cast<float>(modifiedCounter) * 0.01f;
    core::state::macro::MacroWorkflow::syncRuntimeFromActivePage(state.macros, state.pages);

    state.sequencer.pattern.setContentLength(8);
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

core::persistence::ProjectFileStore makeStore(
    core::persistence::ProductFileService& files,
    core::persistence::ProductDirectoryCatalog& catalog
) {
    assert(files.init());
    return core::persistence::ProjectFileStore(files, catalog);
}

oc::type::Result<core::persistence::ProjectListResult> listProjectsSettled(
    core::persistence::ProductFileService& files,
    core::persistence::ProductDirectoryCatalog& catalog,
    core::persistence::ProjectFileStore& store,
    core::persistence::ProjectListEntry* entries,
    uint8_t capacity
) {
    auto listed = store.listProjects(entries, capacity);
    for (uint32_t nowMs = 1U;
         !listed && listed.error().code == oc::type::ErrorCode::HARDWARE_BUSY &&
         nowMs <= 3U;
         ++nowMs) {
        assert(files.persistenceJobs().beginTurn(nowMs));
        catalog.advance(nowMs, false);
        listed = store.listProjects(entries, capacity);
    }
    return listed;
}

void assertLoadedProject(core::persistence::ProjectFileStore& store,
                         const char* expectedName,
                         uint32_t expectedCounter) {
    project::ProjectSnapshot loaded;
    project_file::LoadReport report{};
    auto loadedResult = store.load("p321", loaded, &report);
    assert(loadedResult);
    assert(report.ok());

    test_support::CoreStorages storages;
    auto runtime = makeCoreState(storages);
    assert(project::applyProjectSnapshot(runtime, loaded));

    assert(std::strcmp(runtime.project.metadata.id.data(), "p321") == 0);
    assert(std::strcmp(runtime.project.metadata.name.data(), "p321") == 0);
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
    core::persistence::ProductDirectoryCatalog catalog(files);
    auto store = makeStore(files, catalog);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "ProjectFS", 1);
    auto snapshot = capture(state);

    auto saved = store.save(snapshot);
    assert(saved);
    assert(saved.value().bytesWritten > 0);
    assert(std::strcmp(saved.value().projectPath, "projects/p321.mspj") == 0);
    assert(std::filesystem::is_regular_file(
        testRoot() / "midi-studio" / "projects" / "p321.mspj"
    ));
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "p321.mspj.tmp"
    ));

    assertLoadedProject(store, "ProjectFS", 1);

    std::cout << "[PASS] test_save_load_project_snapshot_roundtrip\n";
}

void test_save_overwrites_existing_project_through_backup_commit() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    core::persistence::ProductDirectoryCatalog catalog(files);
    auto store = makeStore(files, catalog);

    test_support::CoreStorages storages;
    auto first = makeCoreState(storages);
    configureProject(first, "First", 1);
    assert(store.save(capture(first)));

    auto second = makeCoreState(storages);
    configureProject(second, "Second", 2);
    assert(store.save(capture(second)));

    assertLoadedProject(store, "Second", 2);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "p321.mspj.bak"
    ));

    std::cout << "[PASS] test_save_overwrites_existing_project_through_backup_commit\n";
}

void test_stale_tmp_is_replaced_on_save() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    core::persistence::ProductDirectoryCatalog catalog(files);
    auto store = makeStore(files, catalog);

    const uint8_t stale[] = {'s', 't', 'a', 'l', 'e'};
    assert(core::test::writeProductFileFixture(
        files, "tmp/p321.mspj.tmp", 0, stale, sizeof(stale)
    ));

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "TmpClean", 3);
    assert(store.save(capture(state)));

    assertLoadedProject(store, "TmpClean", 3);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "p321.mspj.tmp"
    ));

    std::cout << "[PASS] test_stale_tmp_is_replaced_on_save\n";
}

void test_load_recovers_interrupted_backup_commit() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    core::persistence::ProductDirectoryCatalog catalog(files);
    auto store = makeStore(files, catalog);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "Backup", 4);
    assert(store.save(capture(state)));
    assert(core::test::renameProductFileFixture(
        files, "projects/p321.mspj", "projects/p321.mspj.bak"
    ));

    assertLoadedProject(store, "Backup", 4);
    assert(std::filesystem::is_regular_file(
        testRoot() / "midi-studio" / "projects" / "p321.mspj"
    ));
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "p321.mspj.bak"
    ));

    std::cout << "[PASS] test_load_recovers_interrupted_backup_commit\n";
}

void test_load_rejects_corrupt_current_with_unmapped_backup() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    core::persistence::ProductDirectoryCatalog catalog(files);
    auto store = makeStore(files, catalog);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "Backup", 4);
    assert(store.save(capture(state)));
    assert(core::test::renameProductFileFixture(
        files, "projects/p321.mspj", "projects/p321.mspj.bak"
    ));

    const uint8_t corrupt[] = {'b', 'r', 'o', 'k', 'e', 'n'};
    assert(core::test::writeProductFileFixture(
        files, "projects/p321.mspj", 0, corrupt, sizeof(corrupt)
    ));

    project::ProjectSnapshot loaded;
    project_file::LoadReport report{};
    auto result = store.load("p321", loaded, &report);
    assert(!result);
    assert(result.error().code == oc::type::ErrorCode::STORAGE_CORRUPT);
    assert(std::filesystem::is_regular_file(
        testRoot() / "midi-studio" / "projects" / "p321.mspj"
    ));
    assert(std::filesystem::is_regular_file(
        testRoot() / "midi-studio" / "projects" / "p321.mspj.bak"
    ));

    std::cout << "[PASS] test_load_rejects_corrupt_current_with_unmapped_backup\n";
}

void test_load_reports_corrupt_backup_when_current_is_missing() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    core::persistence::ProductDirectoryCatalog catalog(files);
    auto store = makeStore(files, catalog);

    const uint8_t corrupt[] = {'b', 'r', 'o', 'k', 'e', 'n'};
    assert(core::test::writeProductFileFixture(
        files, "projects/p321.mspj.bak", 0, corrupt, sizeof(corrupt)
    ));

    project::ProjectSnapshot loaded;
    project_file::LoadReport report{};
    auto result = store.load("p321", loaded, &report);
    assert(!result);
    assert(result.error().code == oc::type::ErrorCode::STORAGE_CORRUPT);
    assert(report.failed());

    std::cout << "[PASS] test_load_reports_corrupt_backup_when_current_is_missing\n";
}

void test_save_propagates_current_stat_error_before_commit() {
    resetTestRoot();

    CurrentStatFailureFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    core::persistence::ProductDirectoryCatalog catalog(files);
    auto store = makeStore(files, catalog);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "StatFail", 4);

    filesystem.failCurrentStat = true;
    auto saved = store.save(capture(state));
    assert(!saved);
    assert(saved.error().code == oc::type::ErrorCode::STORAGE_READ_FAILED);
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "projects" / "p321.mspj"
    ));

    std::cout << "[PASS] test_save_propagates_current_stat_error_before_commit\n";
}

void test_list_projects_returns_saved_projects_sorted() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    core::persistence::ProductDirectoryCatalog catalog(files);
    auto store = makeStore(files, catalog);

    test_support::CoreStorages storages;
    auto p003 = makeCoreState(storages);
    configureProject(p003, "Third", 3, "p003");
    assert(store.save(capture(p003)));

    auto p001 = makeCoreState(storages);
    configureProject(p001, "First", 1, "p001");
    assert(store.save(capture(p001)));

    const uint8_t invalid[] = {'x'};
    assert(core::test::writeProductFileFixture(
        files, "projects/BROKEN_.mspj", 0, invalid, sizeof(invalid)
    ));
    assert(core::test::writeProductFileFixture(
        files, "projects/p999.bin", 0, invalid, sizeof(invalid)
    ));

    core::persistence::ProjectListEntry entries[4]{};
    auto listed = listProjectsSettled(files, catalog, store, entries, 4);
    assert(listed);
    assert(listed.value().count == 2);
    assert(!listed.value().truncated);
    assert(std::strcmp(entries[0].id, "p001") == 0);
    assert(std::strcmp(entries[1].id, "p003") == 0);
    assert(entries[0].sizeBytes > 0);
    assert(entries[1].sizeBytes > 0);

    std::cout << "[PASS] test_list_projects_returns_saved_projects_sorted\n";
}

void test_save_load_and_list_max_length_project_slug() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    core::persistence::ProductDirectoryCatalog catalog(files);
    auto store = makeStore(files, catalog);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    auto slug = maxLengthSlug();
    configureProject(state, "LongSlug", 5, slug.data());
    auto saved = store.save(capture(state));
    assert(saved);

    const auto expectedPath =
        testRoot() / "midi-studio" / "projects" / (std::string(slug.data()) + ".mspj");
    assert(std::filesystem::exists(expectedPath));

    project::ProjectSnapshot loaded;
    auto loadedResult = store.load(slug.data(), loaded);
    assert(loadedResult);
    assert(std::strcmp(loaded.project.metadata.id.data(), slug.data()) == 0);
    assert(std::strcmp(loaded.project.metadata.name.data(), slug.data()) == 0);

    core::persistence::ProjectListEntry entries[1]{};
    auto listed = listProjectsSettled(files, catalog, store, entries, 1);
    assert(listed);
    assert(listed.value().count == 1);
    assert(std::strcmp(entries[0].id, slug.data()) == 0);

    std::cout << "[PASS] test_save_load_and_list_max_length_project_slug\n";
}

void test_list_projects_reports_truncation() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService files(filesystem);
    core::persistence::ProductDirectoryCatalog catalog(files);
    auto store = makeStore(files, catalog);

    test_support::CoreStorages storages;
    auto p002 = makeCoreState(storages);
    configureProject(p002, "Second", 2, "p002");
    assert(store.save(capture(p002)));

    auto p001 = makeCoreState(storages);
    configureProject(p001, "First", 1, "p001");
    assert(store.save(capture(p001)));

    core::persistence::ProjectListEntry entries[1]{};
    auto listed = listProjectsSettled(files, catalog, store, entries, 1);
    assert(listed);
    assert(listed.value().count == 1);
    assert(listed.value().truncated);
    assert(std::strcmp(entries[0].id, "p001") == 0);

    std::cout << "[PASS] test_list_projects_reports_truncation\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectFileStore tests\n";
    std::cout << "==============================================\n\n";

    test_save_load_project_snapshot_roundtrip();
    test_save_overwrites_existing_project_through_backup_commit();
    test_stale_tmp_is_replaced_on_save();
    test_load_recovers_interrupted_backup_commit();
    test_load_rejects_corrupt_current_with_unmapped_backup();
    test_load_reports_corrupt_backup_when_current_is_missing();
    test_save_propagates_current_stat_error_before_commit();
    test_list_projects_returns_saved_projects_sorted();
    test_save_load_and_list_max_length_project_slug();
    test_list_projects_reports_truncation();

    resetTestRoot();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

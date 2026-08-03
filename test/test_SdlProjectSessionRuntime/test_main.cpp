#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

#include <oc/impl/HostFileSystem.hpp>
#include <oc/time/Time.hpp>

#include "../../sdl/entry/SdlProjectSessionRuntime.hpp"
#include "../../src/persistence/ProductStorageRecoveryService.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/ProductFileTestMutation.hpp"

namespace {

uint32_t nowMs = 0;

uint32_t testTimeProvider() {
    return nowMs;
}

std::filesystem::path testRoot() {
    return std::filesystem::temp_directory_path() /
           "midi-studio-core-sdl-project-session-runtime-test";
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

void configureProject(core::state::CoreState& state, uint8_t note) {
    state.project.metadata.id.fill('\0');
    std::strncpy(state.project.metadata.id.data(),
                 "sdl-parity",
                 state.project.metadata.id.size() - 1U);
    state.project.metadata.name.fill('\0');
    std::strncpy(state.project.metadata.name.data(),
                 "SDL parity",
                 state.project.metadata.name.size() - 1U);
    state.project.metadata.hasSavedIdentity = true;
    state.sequencer.pattern.setContentLength(8);
    state.sequencer.setStepDataAt(0, note, 100, 75);
    state.sequencer.pattern.toggle(0);
    state.markProjectMutated();
}

void updateUntilSaved(ms::entry::SdlProjectSessionRuntime& runtime,
                      core::state::CoreState& state) {
    for (uint16_t step = 0; step < 256 && state.hasPendingProjectSessionSave(); ++step) {
        runtime.update();
    }
    assert(!state.hasPendingProjectSessionSave());
}

void test_restore_and_firmware_ordered_autosave() {
    resetTestRoot();
    oc::time::setProvider(testTimeProvider);

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService productFiles(filesystem);
    assert(productFiles.init());

    {
        test_support::CoreStorages storages;
        auto state = makeCoreState(storages);
        ms::entry::SdlProjectSessionRuntime runtime(productFiles, state, 1);
        assert(runtime.restoreResult().status ==
               core::persistence::ProjectSessionRestoreService::Status::MISSING);

        nowMs = 10;
        configureProject(state, 67);
        assert(state.hasPendingProjectSessionSave());

        // Match firmware ownership gating: an external product-file transfer
        // blocks both CoreState persistence work and project autosave.
        {
            core::test::ProductFileTestMutation externalWrite(productFiles);
            assert(productFiles.beginWrite(
                externalWrite.lease(), "tmp/external-write.tmp", 1
            ));
            nowMs = 20;
            runtime.update();
            assert(state.hasPendingProjectSessionSave());
            assert(!std::filesystem::exists(
                testRoot() / "midi-studio" / "session" / "current.mspj"
            ));
            assert(productFiles.abortWrite(externalWrite.lease()));
            assert(externalWrite.release());
        }

        updateUntilSaved(runtime, state);
        assert(std::filesystem::exists(
            testRoot() / "midi-studio" / "session" / "current.mspj"
        ));
    }

    {
        test_support::CoreStorages storages;
        auto state = makeCoreState(storages);
        ms::entry::SdlProjectSessionRuntime runtime(productFiles, state, 1);
        assert(runtime.restoreResult().restored());
        assert(std::strcmp(state.project.metadata.id.data(), "sdl-parity") == 0);
        assert(state.sequencer.pattern.length.get() == 8);
        assert(state.sequencer.pattern.isEnabled(0));
        assert(state.sequencer.pattern.note[0] == 67);
    }

    resetTestRoot();
    std::cout << "[PASS] test_restore_and_firmware_ordered_autosave\n";
}

void test_boot_and_hotswap_share_one_retryable_recovery_lease() {
    resetTestRoot();
    oc::time::setProvider(testTimeProvider);

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService productFiles(filesystem);
    assert(productFiles.initForRecovery());
    assert(productFiles.storageState() ==
           core::persistence::ProductStorageState::RECOVERY_PENDING);
    assert((productFiles.storageIdentity() ==
            core::persistence::ProductStorageIdentity{1, 0}));

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, 70);

    core::persistence::ProjectSessionStore store(productFiles);
    core::persistence::ProjectSessionRestoreService restore(store);
    core::persistence::ProjectSessionAutosaveService autosave(store, 1);

    const auto boot =
        core::persistence::ProductStorageRecoveryService::reconcile(
            productFiles,
            restore,
            autosave,
            state,
            core::persistence::ProductStorageRecoveryMode::BOOT
        );
    assert(boot.recovered());
    assert(boot.sessionRestoreStatus ==
           core::persistence::ProjectSessionRestoreService::Status::MISSING);
    assert(boot.sessionSaveStatus ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVED);
    assert(productFiles.storageState() ==
           core::persistence::ProductStorageState::READY);
    // Layout, settings reconciliation and the complete Project commit share
    // one lease, so all physical calls publish exactly one storage epoch.
    assert((productFiles.storageIdentity() ==
            core::persistence::ProductStorageIdentity{1, 1}));
    assert(!state.hasPendingProjectSessionSave());

    configureProject(state, 71);
    productFiles.markMediaUnavailable();
    assert(productFiles.storageState() ==
           core::persistence::ProductStorageState::ABSENT);
    assert((productFiles.storageIdentity() ==
            core::persistence::ProductStorageIdentity{1, 1}));

    storages.settings.setFaultMode(
        test_support::MemoryStorage::FaultMode::COMMIT_FAIL
    );
    const auto failed =
        core::persistence::ProductStorageRecoveryService::reconcile(
            productFiles,
            restore,
            autosave,
            state,
            core::persistence::ProductStorageRecoveryMode::HOT_SWAP
        );
    assert(failed.status ==
           core::persistence::ProductStorageRecoveryStatus::SETTINGS_FAILED);
    assert(productFiles.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assert((productFiles.storageIdentity() ==
            core::persistence::ProductStorageIdentity{2, 0}));
    assert(state.hasPendingProjectSessionSave());
    const auto blocked = productFiles.stat(
        core::persistence::ProjectSessionStore::CURRENT_SESSION_PATH
    );
    assert(!blocked);
    assert(blocked.error().code == oc::type::ErrorCode::HARDWARE_BUSY);

    storages.settings.setFaultMode(test_support::MemoryStorage::FaultMode::NONE);
    const auto retried =
        core::persistence::ProductStorageRecoveryService::reconcile(
            productFiles,
            restore,
            autosave,
            state,
            core::persistence::ProductStorageRecoveryMode::HOT_SWAP
        );
    assert(retried.recovered());
    assert(retried.sessionSaveStatus ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVED);
    assert(productFiles.storageState() ==
           core::persistence::ProductStorageState::READY);
    // Retry stays on generation 2; the exact RAM save publishes its first and
    // only epoch for the reinserted medium.
    assert((productFiles.storageIdentity() ==
            core::persistence::ProductStorageIdentity{2, 1}));
    assert(!state.hasPendingProjectSessionSave());

    test_support::CoreStorages restoredStorages;
    auto restoredState = makeCoreState(restoredStorages);
    const auto restored = restore.restore(restoredState);
    assert(restored.restored());
    assert(restoredState.sequencer.pattern.note[0] == 71);

    resetTestRoot();
    std::cout
        << "[PASS] test_boot_and_hotswap_share_one_retryable_recovery_lease\n";
}

}  // namespace

int main() {
    test_restore_and_firmware_ordered_autosave();
    test_boot_and_hotswap_share_one_retryable_recovery_lease();
    return 0;
}

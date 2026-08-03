#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <utility>

#include <oc/impl/HostFileSystem.hpp>
#include <oc/time/Time.hpp>

#include "../../sdl/entry/SdlProjectSessionRuntime.hpp"
#include "../../src/persistence/DeviceSettingsStorageLayout.hpp"
#include "../../src/persistence/ProductStorageRecoveryService.hpp"
#include "../../src/persistence/ProductStorageRecoveryPlan.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/ProductFileTestMutation.hpp"

namespace {

uint32_t nowMs = 0;

uint32_t testTimeProvider() {
    return nowMs;
}

class SessionSaveFaultFileSystem final : public oc::impl::HostFileSystem {
public:
    explicit SessionSaveFaultFileSystem(const char* rootPath)
        : oc::impl::HostFileSystem(rootPath) {}

    void setSessionSaveFault(bool enabled) {
        session_save_fault_ = enabled;
    }

    uint32_t sessionBeginAttempts() const {
        return session_begin_attempts_;
    }

    oc::type::Result<void> beginWrite(
        const char* path,
        uint32_t expectedSize
    ) override {
        if (path != nullptr && std::strstr(path, "session.current.tmp") != nullptr) {
            ++session_begin_attempts_;
            if (session_save_fault_) {
                return oc::type::Result<void>::err({
                    oc::type::ErrorCode::STORAGE_WRITE_FAILED,
                    "injected session begin-write failure",
                });
            }
        }
        return oc::impl::HostFileSystem::beginWrite(path, expectedSize);
    }

private:
    uint32_t session_begin_attempts_ = 0;
    bool session_save_fault_ = false;
};

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

        // An external transfer blocks autosave, but state maintenance and the
        // single foreground scheduling boundary continue every pass.
        {
            core::test::ProductFileTestMutation externalWrite(productFiles);
            assert(productFiles.beginWrite(
                externalWrite.lease(), "tmp/external-write.tmp", 1
            ));
            auto scheduledResult = productFiles.persistenceJobs().admit({
                .owner = core::persistence::ProductPersistenceJobOwner::PROJECT_CATALOG,
                .nowMs = 20U,
                .quota = core::persistence::PRODUCT_PERSISTENCE_QUOTA_RAW_CATALOG,
            });
            assert(scheduledResult);
            auto scheduled = std::move(scheduledResult.value());

            state.statusBar.pulseNoteIn(20U);
            assert(state.statusBar.noteInActive.get());
            nowMs = 1000U;
            runtime.update();
            assert(!state.statusBar.noteInActive.get());
            assert(state.hasPendingProjectSessionSave());
            assert(!std::filesystem::exists(
                testRoot() / "midi-studio" / "session" / "current.mspj"
            ));

            auto& jobs = productFiles.persistenceJobs();
            assert(jobs.claimAdvance(scheduled, nowMs));
            const auto duplicateClaim = jobs.claimAdvance(scheduled, nowMs);
            assert(!duplicateClaim);
            assert(duplicateClaim.error().code == oc::type::ErrorCode::INVALID_STATE);
            assert(jobs.finishAdvance(scheduled, {}, true));
            assert(jobs.cancel(scheduled));

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
    // RAM settings changed before this media generation was lost. Recovery
    // must publish the new value, so the injected commit fault is meaningful.
    state.midiSync.mode.set(core::state::MidiSyncMode::SLAVE);
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

void test_session_save_failure_preserves_stage_and_does_not_rewrite_settings() {
    resetTestRoot();
    oc::time::setProvider(testTimeProvider);

    SessionSaveFaultFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService productFiles(filesystem);
    assert(productFiles.initForRecovery());

    test_support::CoreStorages storages;
    namespace SettingsLayout = core::persistence::device_settings::layout;
    const uint32_t magic = SettingsLayout::MAGIC;
    const uint8_t legacyVersion =
        static_cast<uint8_t>(SettingsLayout::VERSION - 1U);
    assert(storages.settings.write(
        SettingsLayout::ADDR_MAGIC,
        reinterpret_cast<const uint8_t*>(&magic),
        sizeof(magic)
    ) == sizeof(magic));
    assert(storages.settings.write(
        SettingsLayout::ADDR_VERSION,
        &legacyVersion,
        sizeof(legacyVersion)
    ) == sizeof(legacyVersion));
    assert(storages.settings.commit());

    auto state = makeCoreState(storages);
    configureProject(state, 72);
    const int commitsBeforeRecovery = storages.settings.commitCount;

    core::persistence::ProjectSessionStore store(productFiles);
    core::persistence::ProjectSessionRestoreService restore(store);
    core::persistence::ProjectSessionAutosaveService autosave(store, 1);
    filesystem.setSessionSaveFault(true);

    const auto failedBoot =
        core::persistence::ProductStorageRecoveryService::reconcile(
            productFiles,
            restore,
            autosave,
            state,
            core::persistence::ProductStorageRecoveryMode::BOOT
        );
    assert(failedBoot.status ==
           core::persistence::ProductStorageRecoveryStatus::SESSION_SAVE_FAILED);
    assert(core::persistence::productStorageRecoveryRequiresMediaChange(
        failedBoot.status
    ));
    assert(failedBoot.error == oc::type::ErrorCode::STORAGE_WRITE_FAILED);
    assert(failedBoot.errorContext != nullptr);
    assert(std::strcmp(
        failedBoot.errorContext,
        "injected session begin-write failure"
    ) == 0);
    assert(failedBoot.sessionSaveFailureStage ==
           core::persistence::ProjectSessionAutosaveService::FailureStage::WRITE);
    assert(storages.settings.commitCount == commitsBeforeRecovery + 1);
    const int commitsAfterMigration = storages.settings.commitCount;

    const auto failedRetry =
        core::persistence::ProductStorageRecoveryService::reconcile(
            productFiles,
            restore,
            autosave,
            state,
            core::persistence::ProductStorageRecoveryMode::HOT_SWAP
        );
    assert(failedRetry.status ==
           core::persistence::ProductStorageRecoveryStatus::SESSION_SAVE_FAILED);
    assert(failedRetry.sessionSaveFailureStage ==
           core::persistence::ProjectSessionAutosaveService::FailureStage::WRITE);
    assert(storages.settings.commitCount == commitsAfterMigration);

    filesystem.setSessionSaveFault(false);
    const auto recovered =
        core::persistence::ProductStorageRecoveryService::reconcile(
            productFiles,
            restore,
            autosave,
            state,
            core::persistence::ProductStorageRecoveryMode::HOT_SWAP
        );
    assert(recovered.recovered());
    assert(!core::persistence::productStorageRecoveryRequiresMediaChange(
        recovered.status
    ));
    assert(storages.settings.commitCount == commitsAfterMigration);
    assert(filesystem.sessionBeginAttempts() == 3U);
    assert(!state.hasPendingProjectSessionSave());

    resetTestRoot();
    std::cout
        << "[PASS] session save failure preserves stage and settings idempotence\n";
}

void test_hotswap_recovery_is_stopped_only_and_cooperative() {
    resetTestRoot();
    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    core::persistence::ProductFileService productFiles(filesystem);
    assert(productFiles.initForRecovery());
    // A fixed hidden root is the durable recursive-delete continuation. A
    // reconstructed hot-swap plan must drain it before publishing READY.
    assert(std::filesystem::create_directories(
        testRoot() / "midi-studio" / "tmp" / "rpc-d" / "a" / "b"
    ));

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, 73);
    core::persistence::ProjectSessionStore store(productFiles);
    core::persistence::ProjectSessionRestoreService restore(store);
    core::persistence::ProjectSessionAutosaveService autosave(store, 1);
    auto plan = std::make_unique<
        core::persistence::ProductStorageRecoveryPlan>();
    assert(plan->begin(
        productFiles,
        autosave,
        state,
        core::persistence::ProductStorageRecoveryMode::HOT_SWAP
    ));

    auto& jobs = productFiles.persistenceJobs();
    auto admitted = jobs.admit({
        .owner = core::persistence::ProductPersistenceJobOwner::STORAGE_RECOVERY,
        .nowMs = 100U,
        .quota = plan->nextWorkQuota(autosave),
    });
    assert(admitted);
    auto token = std::move(admitted.value());

    for (uint32_t turn = 0; turn < 3U; ++turn) {
        assert(jobs.beginTurn(100U + turn));
        core::persistence::ProductPersistenceJobSnapshot snapshot{};
        assert(jobs.inspect(token, snapshot));
        assert(snapshot.metrics.advances == 0U);
        assert(!std::filesystem::exists(
            testRoot() / "midi-studio" / "session" / "current.mspj"
        ));
    }

    uint16_t turns = 0U;
    while (plan->active() && turns < 512U) {
        ++turns;
        state.update();
        const uint32_t turnNow = 200U + turns;
        assert(jobs.beginTurn(turnNow));
        assert(jobs.isActive(token));
        const auto quota = plan->nextWorkQuota(autosave);
        assert(jobs.prepareAdvance(token, quota));
        assert(jobs.claimAdvance(token, turnNow));

        core::persistence::ProductPersistenceWorkUsage usage{};
        bool terminal = false;
        {
            auto measuredResult = productFiles.measurePersistenceWork(usage);
            assert(measuredResult);
            auto measurement = std::move(measuredResult.value());
            terminal = plan->advance(
                productFiles,
                restore,
                autosave,
                state,
                &measurement
            );
        }
        usage.bytes += plan->lastWorkBytes();
        assert(usage.bytes <= quota.maxBytes());
        if (usage.filesystemCalls > quota.maxFilesystemCalls()) {
            std::cerr << "recovery quota mismatch turn=" << turns
                      << " calls=" << static_cast<unsigned>(usage.filesystemCalls)
                      << " max=" << static_cast<unsigned>(quota.maxFilesystemCalls())
                      << " bytes=" << usage.bytes
                      << " maxBytes=" << quota.maxBytes() << '\n';
        }
        assert(usage.filesystemCalls <= quota.maxFilesystemCalls());
        assert(usage.allocations <= quota.maxAllocations());
        assert(usage.entries <= quota.maxEntries());
        assert(usage.nodes <= quota.maxNodes());
        assert(jobs.finishAdvance(token, usage, true));
        if (terminal) break;
    }

    assert(plan->terminal());
    assert(plan->result().recovered());
    assert(turns > 10U);
    assert(jobs.complete(token));
    assert(jobs.depth() == 0U);
    assert(jobs.highWater() == 1U);
    assert(productFiles.storageState() ==
           core::persistence::ProductStorageState::READY);
    assert(!state.hasPendingProjectSessionSave());
    assert(std::filesystem::exists(
        testRoot() / "midi-studio" / "session" / "current.mspj"
    ));
    assert(!std::filesystem::exists(
        testRoot() / "midi-studio" / "tmp" / "rpc-d"
    ));

    resetTestRoot();
    std::cout << "[PASS] hot-swap recovery is stopped-only and cooperative ("
              << turns << " advances)\n";
}

}  // namespace

int main() {
    test_restore_and_firmware_ordered_autosave();
    test_boot_and_hotswap_share_one_retryable_recovery_lease();
    test_session_save_failure_preserves_stage_and_does_not_rewrite_settings();
    test_hotswap_recovery_is_stopped_only_and_cooperative();
    return 0;
}

#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <limits>
#include <utility>

#include <oc/impl/HostFileSystem.hpp>
#include <oc/time/Time.hpp>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/handler/macro/MacroPerformanceDomainServices.hpp"
#include "../../src/persistence/ProductFileService.hpp"
#include "../../src/persistence/ProjectSessionAutosaveService.hpp"
#include "../../src/persistence/ProjectSessionRestoreService.hpp"
#include "../../src/persistence/ProjectSessionStore.hpp"
#include "../../src/persistence/ProductStorageRecoveryService.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/macro/MacroWorkflow.hpp"
#include "../../src/state/modulation/ProjectControlMacroOps.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../../src/state/sequencer/SequencerCcLaneDomain.hpp"
#include "../../src/state/sequencer/SequencerCcLanePatternOps.hpp"
#include "../../src/state/sequencer/SequencerGraphOps.hpp"
#include "../support/CoreStorages.hpp"

namespace core::state::testing {

struct ProjectSessionTokenTestAccess {
    static void seed(CoreState& state,
                     project::ProjectSessionIdentity session,
                     uint32_t mutationEpoch,
                     uint32_t requestId) {
        state.projectSessionControl_.session = session;
        state.projectSessionControl_.mutationEpoch = mutationEpoch;
        state.projectSessionControl_.requestId = requestId;
        state.projectSessionControl_.requestTimestampMs = 0U;
        state.projectSessionControl_.trackingEnabled = true;
        state.projectSessionControl_.savePending = false;
    }
};

}  // namespace core::state::testing

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;
namespace modulation = core::state::modulation;

uint32_t mockTimeMs() {
    return 0;
}

uint32_t g_slow_micros = 0U;

uint32_t slowMicros() {
    const uint32_t current = g_slow_micros;
    g_slow_micros +=
        core::persistence::PRODUCT_PERSISTENCE_SOFT_ADVANCE_WALL_MICROS + 1U;
    return current;
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

core::persistence::ProjectSessionAutosaveService::Result updateAutosave(
    core::persistence::ProductFileService& files,
    core::persistence::ProjectSessionAutosaveService& autosave,
    core::state::CoreState& state,
    uint32_t nowMs,
    bool mutationPending = false,
    bool playbackActive = false
) {
    assert(files.persistenceJobs().beginTurn(nowMs));
    return autosave.update(state, nowMs, mutationPending, playbackActive);
}

core::persistence::ProjectSessionAutosaveService::Result updateUntilSettled(
    core::persistence::ProductFileService& files,
    core::persistence::ProjectSessionAutosaveService& autosave,
    core::state::CoreState& state,
    uint32_t nowMs
) {
    for (uint16_t step = 0; step < 384; ++step) {
        auto result = updateAutosave(files, autosave, state, nowMs);
        if (result.status !=
            core::persistence::ProjectSessionAutosaveService::Status::SAVING) {
            return result;
        }
    }
    assert(false && "autosave did not settle");
    return {};
}

void advanceUntilWriteSession(
    core::persistence::ProductFileService& files,
    core::persistence::ProjectSessionAutosaveService& autosave,
    core::state::CoreState& state,
    uint32_t nowMs
) {
    for (uint16_t step = 0; step < 256U; ++step) {
        const auto progress = updateAutosave(files, autosave, state, nowMs);
        assert(progress.status ==
               core::persistence::ProjectSessionAutosaveService::Status::SAVING);
        if (files.writeSessionActive()) return;
    }
    assert(false && "autosave did not open its bounded write session");
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
    auto waiting = updateAutosave(files, autosave, state, requestedAt + 999U);
    assert(waiting.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    auto started = updateAutosave(files, autosave, state, requestedAt + 1000U);
    assert(started.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    assertNoCurrentSessionFile();

    auto saved = updateUntilSettled(files, autosave, state, requestedAt + 1000U);
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

    auto stillWaiting = updateAutosave(files, autosave, state, secondRequestAt + 999U);
    assert(stillWaiting.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    auto saved = updateUntilSettled(files, autosave, state, secondRequestAt + 1000U);
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
    auto blocked = updateAutosave(files, autosave, state, requestedAt + 100U, true);
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    auto saved = updateUntilSettled(files, autosave, state, requestedAt + 101U);
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
    auto blocked = updateAutosave(
        files,
        autosave,
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
    auto saved = updateUntilSettled(files, autosave, state, flushedRequestAt + 100U);
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
    auto started = updateAutosave(files, autosave, state, firstRequestAt + 100U);
    assert(started.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);

    state.sequencer.setStepDataAt(0, 77, 100, 75);
    state.markProjectMutated();
    const uint32_t latestRequestAt = state.projectSessionSaveTimestampMs();

    auto stale = updateAutosave(files, autosave, state, latestRequestAt + 99U);
    assert(stale.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assertNoCurrentSessionFile();

    auto saved = updateUntilSettled(files, autosave, state, latestRequestAt + 100U);
    assert(saved.saved());
    assert(saved.modifiedCounter == state.project.metadata.modifiedCounter);
    assertCurrentSessionNote(files, 77);

    std::cout << "[PASS] test_mutation_during_capture_restarts_from_latest_revision\n";
}

void test_same_mutation_second_request_invalidates_in_flight_capture() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p013", 73);
    state.markProjectMutated();
    const auto firstToken = state.projectSessionSaveToken();

    const auto started = updateAutosave(
        files,
        autosave,
        state,
        state.projectSessionSaveTimestampMs() + 100U
    );
    assert(started.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);

    const auto secondToken = state.requestProjectSessionSave();
    assert(secondToken.session == firstToken.session);
    assert(secondToken.mutationEpoch == firstToken.mutationEpoch);
    assert(secondToken.modifiedCounter == firstToken.modifiedCounter);
    assert(secondToken.requestId == firstToken.requestId + 1U);

    const auto waiting = updateAutosave(
        files,
        autosave,
        state,
        state.projectSessionSaveTimestampMs() + 99U
    );
    assert(waiting.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    const auto saved = updateUntilSettled(
        files,
        autosave,
        state,
        state.projectSessionSaveTimestampMs() + 100U
    );
    assert(saved.saved());
    assert(!state.hasPendingProjectSessionSave());
    assertCurrentSessionNote(files, 73);

    std::cout
        << "[PASS] same-mutation second request invalidates capture\n";
}

void test_equal_counter_project_replacement_cancels_stale_save() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p014", 74);
    state.markProjectMutated();
    const auto projectAToken = state.projectSessionSaveToken();

    test_support::CoreStorages replacementStorages;
    auto replacementState = makeCoreState(replacementStorages);
    configureProject(replacementState, "p015", 75);
    replacementState.project.metadata.modifiedCounter =
        state.project.metadata.modifiedCounter;
    project::ProjectSnapshot replacement;
    assert(project::captureProjectSnapshot(replacementState, replacement));

    const uint32_t requestedAt = state.projectSessionSaveTimestampMs();
    advanceUntilWriteSession(files, autosave, state, requestedAt + 100U);

    const auto tmpPath =
        testRoot() / "midi-studio" / "tmp" / "session.current.tmp";
    assert(std::filesystem::is_regular_file(tmpPath));
    assertNoCurrentSessionFile();

    assert(project::applyProjectSnapshot(state, replacement));
    state.requestProjectSessionSave();
    const auto projectBToken = state.projectSessionSaveToken();
    assert(projectBToken.modifiedCounter == projectAToken.modifiedCounter);
    assert(projectBToken.session != projectAToken.session);
    assert(state.hasPendingProjectSessionSave());

    const auto waiting = updateAutosave(
        files,
        autosave,
        state,
        state.projectSessionSaveTimestampMs() + 99U
    );
    assert(waiting.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(!std::filesystem::exists(tmpPath));
    assertNoCurrentSessionFile();
    assert(state.hasPendingProjectSessionSave());

    const auto saved = updateUntilSettled(
        files,
        autosave,
        state,
        state.projectSessionSaveTimestampMs() + 100U
    );
    assert(saved.saved());
    assertCurrentSessionNote(files, 75);

    std::cout
        << "[PASS] equal-counter Project replacement cancels stale save\n";
}

void test_exact_acknowledgement_rejects_a_stale_completion() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p016", 76);
    state.markProjectMutated();
    const auto completedToken = state.projectSessionSaveToken();

    const auto currentToken = state.requestProjectSessionSave();
    assert(currentToken.requestId == completedToken.requestId + 1U);
    assert(!state.acknowledgeProjectSessionSave(completedToken));
    assert(state.hasPendingProjectSessionSave());
    assert(state.projectSessionSaveToken() == currentToken);

    assert(state.acknowledgeProjectSessionSave(currentToken));
    assert(!state.hasPendingProjectSessionSave());
    assert(!state.acknowledgeProjectSessionSave(currentToken));

    std::cout
        << "[PASS] exact acknowledgement rejects stale completion\n";
}

void test_checked_token_rollover_rotates_session_identity() {
    constexpr uint32_t maximum = std::numeric_limits<uint32_t>::max();
    using Access = core::state::testing::ProjectSessionTokenTestAccess;

    {
        test_support::CoreStorages storages;
        auto state = makeCoreState(storages);
        configureProject(state, "p017", 77);
        Access::seed(state, {7U, 9U}, 41U, maximum);
        state.project.metadata.modifiedCounter = 55U;

        const auto token = state.requestProjectSessionSave();
        assert(token.session.bootGeneration == 7U);
        assert(token.session.sessionEpoch == 10U);
        assert(token.mutationEpoch == 41U);
        assert(token.requestId == 1U);
        assert(token.modifiedCounter == 55U);
    }

    {
        test_support::CoreStorages storages;
        auto state = makeCoreState(storages);
        configureProject(state, "p018", 78);
        Access::seed(state, {11U, 17U}, maximum, 12U);
        state.project.metadata.modifiedCounter = 90U;

        state.markProjectMutated();
        const auto token = state.projectSessionSaveToken();
        assert(token.session.bootGeneration == 11U);
        assert(token.session.sessionEpoch == 18U);
        assert(token.mutationEpoch == 1U);
        assert(token.requestId == 1U);
        assert(token.modifiedCounter == 91U);
        assert(state.hasPendingProjectSessionSave());
    }

    {
        test_support::CoreStorages storages;
        auto state = makeCoreState(storages);
        configureProject(state, "p021", 81);
        Access::seed(state, {12U, 21U}, 33U, 14U);
        state.project.metadata.modifiedCounter = maximum;

        state.markProjectMutated();
        const auto token = state.projectSessionSaveToken();
        assert(token.session.bootGeneration == 12U);
        assert(token.session.sessionEpoch == 22U);
        assert(token.mutationEpoch == 34U);
        assert(token.requestId == 1U);
        assert(token.modifiedCounter == 1U);
        assert(state.hasPendingProjectSessionSave());
    }

    {
        test_support::CoreStorages storages;
        auto state = makeCoreState(storages);
        configureProject(state, "p022", 82);
        Access::seed(state, {13U, 30U}, maximum, 15U);
        state.project.metadata.modifiedCounter = maximum;

        state.markProjectMutated();
        const auto token = state.projectSessionSaveToken();
        assert(token.session.bootGeneration == 13U);
        assert(token.session.sessionEpoch == 31U);
        assert(token.mutationEpoch == 1U);
        assert(token.requestId == 1U);
        assert(token.modifiedCounter == 1U);
        assert(state.hasPendingProjectSessionSave());
    }

    {
        test_support::CoreStorages storages;
        auto state = makeCoreState(storages);
        configureProject(state, "p019", 79);
        Access::seed(state, {23U, maximum}, 8U, maximum);

        const auto token = state.requestProjectSessionSave();
        assert(token.session.bootGeneration == 24U);
        assert(token.session.sessionEpoch == 1U);
        assert(token.requestId == 1U);
    }

    {
        test_support::CoreStorages storages;
        auto state = makeCoreState(storages);
        configureProject(state, "p020", 80);
        Access::seed(state, {maximum, maximum}, 9U, maximum);

        const auto before = state.projectSessionSaveToken();
        const auto after = state.requestProjectSessionSave();
        assert(after == before);
        assert(!state.hasPendingProjectSessionSave());
        assert(state.requestProjectSessionSave() == before);
    }

    std::cout << "[PASS] checked token rollover rotates session identity\n";
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
    auto started = updateAutosave(files, autosave, state, requestedAt + 100U);
    assert(started.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);

    auto blocked = updateAutosave(
        files,
        autosave,
        state,
        requestedAt + 101U,
        true
    );
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assertNoCurrentSessionFile();

    auto saved = updateUntilSettled(files, autosave, state, requestedAt + 102U);
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
    advanceUntilWriteSession(files, autosave, state, requestedAt + 100U);

    const auto tmpPath =
        testRoot() / "midi-studio" / "tmp" / "session.current.tmp";
    assert(std::filesystem::is_regular_file(tmpPath));
    assertNoCurrentSessionFile();
    assert(store.saveCurrentWriteSessionActive());
    assert(files.writeSessionActive());

    state.sequencer.setStepDataAt(0, 78, 100, 75);
    state.markProjectMutated();
    const uint32_t latestRequestAt = state.projectSessionSaveTimestampMs();

    const auto waiting = updateAutosave(
        files,
        autosave,
        state,
        latestRequestAt + 99U
    );
    assert(waiting.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(!std::filesystem::exists(tmpPath));
    assertNoCurrentSessionFile();
    assert(!store.saveCurrentWriteSessionActive());
    assert(!files.writeSessionActive());

    const auto saved = updateUntilSettled(
        files,
        autosave,
        state,
        latestRequestAt + 100U
    );
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
    advanceUntilWriteSession(files, autosave, state, requestedAt + 100U);

    const auto tmpPath =
        testRoot() / "midi-studio" / "tmp" / "session.current.tmp";
    assert(std::filesystem::is_regular_file(tmpPath));
    assert(store.saveCurrentWriteSessionActive());
    assert(files.writeSessionActive());

    const auto macros = core::handler::MacroPerformanceDomainServices::fromCoreState(state);
    macros.setManualValue(0, 0.8f);
    assert(state.hasPendingProjectMutationCoalescing());

    const auto blocked = updateAutosave(
        files,
        autosave,
        state,
        requestedAt + 101U,
        state.hasPendingProjectMutationCoalescing()
    );
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assert(!std::filesystem::exists(tmpPath));
    assert(!store.saveCurrentWriteSessionActive());
    assert(!files.writeSessionActive());
    assert(state.hasPendingProjectSessionSave());

    state.flushProjectMutationCoalescing();
    const uint32_t latestRequestAt = state.projectSessionSaveTimestampMs();
    const auto saved = updateUntilSettled(
        files,
        autosave,
        state,
        latestRequestAt + 100U
    );
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
    const auto started = updateAutosave(files, autosave, state, requestedAt + 100U);
    assert(started.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);

    const auto begun = beginLfoAudition(state);
    assert(begun.changed());
    assert(state.hasPendingProjectTransaction());

    const auto blocked = updateAutosave(files, autosave, state, requestedAt + 101U);
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();
    assert(!store.saveCurrentWriteSessionActive());
    assert(!files.writeSessionActive());

    assert(state.macroHistory.abortPendingModulatorAudition(state.pages));
    assert(!state.hasPendingProjectTransaction());
    const auto saved = updateUntilSettled(files, autosave, state, requestedAt + 102U);
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
        advanceUntilWriteSession(files, autosave, state, requestedAt + 100U);
        assert(store.saveCurrentWriteSessionActive());
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

void test_playback_blocks_admission_and_freezes_admitted_work() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p023", 83);
    state.markProjectMutated();
    const uint32_t dueAt = state.projectSessionSaveTimestampMs() + 100U;

    const auto blocked = updateAutosave(
        files, autosave, state, dueAt, false, true
    );
    assert(blocked.status ==
           core::persistence::ProjectSessionAutosaveService::Status::BLOCKED);
    assert(files.persistenceJobs().depth() == 0U);
    assertNoCurrentSessionFile();

    const auto started = updateAutosave(files, autosave, state, dueAt);
    assert(started.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    core::persistence::ProductPersistenceJobSnapshot beforeCapturePause{};
    assert(autosave.inspectPersistenceJob(beforeCapturePause));
    assert(beforeCapturePause.metrics.advances == 1U);

    for (uint8_t turn = 0U; turn < 8U; ++turn) {
        const auto paused = updateAutosave(
            files,
            autosave,
            state,
            dueAt + 1U + turn,
            false,
            true
        );
        assert(paused.status ==
               core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    }
    core::persistence::ProductPersistenceJobSnapshot afterCapturePause{};
    assert(autosave.inspectPersistenceJob(afterCapturePause));
    assert(afterCapturePause.metrics.advances ==
           beforeCapturePause.metrics.advances);
    assertNoCurrentSessionFile();

    advanceUntilWriteSession(files, autosave, state, dueAt + 10U);
    core::persistence::ProductPersistenceJobSnapshot beforeWritePause{};
    assert(autosave.inspectPersistenceJob(beforeWritePause));
    const auto tmpPath =
        testRoot() / "midi-studio" / "tmp" / "session.current.tmp";
    assert(std::filesystem::is_regular_file(tmpPath));
    const auto bytesBeforePause = std::filesystem::file_size(tmpPath);

    for (uint8_t turn = 0U; turn < 8U; ++turn) {
        const auto paused = updateAutosave(
            files,
            autosave,
            state,
            dueAt + 20U + turn,
            false,
            true
        );
        assert(paused.status ==
               core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    }
    core::persistence::ProductPersistenceJobSnapshot afterWritePause{};
    assert(autosave.inspectPersistenceJob(afterWritePause));
    assert(afterWritePause.metrics.advances == beforeWritePause.metrics.advances);
    assert(std::filesystem::file_size(tmpPath) == bytesBeforePause);
    assert(store.saveCurrentWriteSessionActive());

    const auto saved = updateUntilSettled(
        files, autosave, state, dueAt + 30U
    );
    assert(saved.saved());
    assertCurrentSessionNote(files, 83);

    std::cout << "[PASS] playback blocks admission and freezes admitted work\n";
}

void test_admitted_autosave_performs_no_extmem_allocation() {
#if defined(MS_CORE_ENABLE_EXTMEM_FAILURE_INJECTION)
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p024", 84);
    assert(core::state::sequencer::ensureGraphRoot(state.sequencer.pattern));
    auto* ccLanes = core::state::sequencer::ensureSequencerCcLaneBank(
        state.sequencer.pattern
    );
    assert(ccLanes != nullptr);
    core::state::sequencer::SequencerCcLaneDraft ccLane{};
    ccLane.destination.controller = 74U;
    assert(core::state::sequencer::createSequencerCcLane(
        *ccLanes,
        0U,
        ccLane
    ).changed());
    state.markProjectMutated();
    const uint32_t dueAt = state.projectSessionSaveTimestampMs() + 100U;
    auto progress = updateAutosave(files, autosave, state, dueAt);
    assert(progress.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);

    for (uint16_t turn = 0U; turn < 384U; ++turn) {
        {
            core::app::testing::ScopedExtmemAllocationFailure failure(1U);
            progress = updateAutosave(files, autosave, state, dueAt + turn + 1U);
            assert(core::app::testing::extmemAllocationAttempt == 0U);
        }
        if (progress.status !=
            core::persistence::ProjectSessionAutosaveService::Status::SAVING) {
            break;
        }
    }
    assert(progress.saved());
    assert(files.persistenceJobs().depth() == 0U);
    assert(files.persistenceJobs().highWater() == 1U);
    assertCurrentSessionNote(files, 84);
#endif

    std::cout << "[PASS] admitted autosave performs no EXTMEM allocation\n";
}

void test_stale_capture_after_durable_mapping_preserves_recovery_evidence() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionRestoreService restore(store);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p025", 85);
    state.markProjectMutated();
    const uint32_t dueAt = state.projectSessionSaveTimestampMs() + 100U;
    const auto slotA =
        testRoot() / "midi-studio" / "tmp" / "rpc-product-file-a.journal";
    const auto slotB =
        testRoot() / "midi-studio" / "tmp" / "rpc-product-file-b.journal";
    const auto tmpPath =
        testRoot() / "midi-studio" / "tmp" / "session.current.tmp";

    bool durableMappingObserved = false;
    for (uint16_t turn = 0U; turn < 384U; ++turn) {
        const auto progress = updateAutosave(
            files, autosave, state, dueAt + turn
        );
        assert(progress.status ==
               core::persistence::ProjectSessionAutosaveService::Status::SAVING);
        durableMappingObserved = std::filesystem::exists(slotA) ||
                                 std::filesystem::exists(slotB);
        if (durableMappingObserved) break;
    }
    assert(durableMappingObserved);
    assert(std::filesystem::exists(tmpPath));
    assert(files.storageState() ==
           core::persistence::ProductStorageState::READY);

    state.sequencer.setStepDataAt(0U, 86U, 100U, 75U);
    state.markProjectMutated();
    const uint32_t latestDueAt = state.projectSessionSaveTimestampMs() + 100U;
    const auto waiting = updateAutosave(
        files, autosave, state, latestDueAt - 1U
    );
    assert(waiting.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(files.storageState() ==
           core::persistence::ProductStorageState::DEGRADED);
    assert(std::filesystem::exists(tmpPath));
    assert(std::filesystem::exists(slotA) || std::filesystem::exists(slotB));
    assertNoCurrentSessionFile();

    const auto recovered =
        core::persistence::ProductStorageRecoveryService::reconcile(
            files,
            restore,
            autosave,
            state,
            core::persistence::ProductStorageRecoveryMode::HOT_SWAP
        );
    assert(recovered.recovered());
    assert(files.storageState() ==
           core::persistence::ProductStorageState::READY);
    assert(!state.hasPendingProjectSessionSave());
    assertCurrentSessionNote(files, 86U);

    std::cout
        << "[PASS] stale durable mapping preserves and reconciles evidence\n";
}

void test_encode_advance_is_finite_measured_and_io_free() {
    resetTestRoot();
    g_slow_micros = 0U;

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(
        store,
        100U,
        &slowMicros
    );

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p026", 87U);
    state.markProjectMutated();
    const uint32_t dueAt = state.projectSessionSaveTimestampMs() + 100U;

    bool encodeObserved = false;
    core::persistence::ProjectSessionAutosaveService::Result progress{};
    for (uint16_t turn = 0U; turn < 384U; ++turn) {
        progress = updateAutosave(files, autosave, state, dueAt + turn);
        core::persistence::ProductPersistenceJobSnapshot snapshot{};
        if (autosave.inspectPersistenceJob(snapshot) &&
            snapshot.quota.maxBytes() ==
                core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROJECT_ENCODE.maxBytes() &&
            snapshot.metrics.advances != 0U) {
            encodeObserved = true;
            assert(snapshot.lastUsage.bytes > 0U);
            assert(snapshot.lastUsage.bytes <= 512U * 1024U);
            assert(snapshot.lastUsage.filesystemCalls == 0U);
            assert(snapshot.lastUsage.allocations == 0U);
            assert(snapshot.lastUsage.wallMicros >
                   core::persistence::PRODUCT_PERSISTENCE_SOFT_ADVANCE_WALL_MICROS);
            assert(snapshot.wallOverruns > 0U);
        }
        if (progress.status !=
            core::persistence::ProjectSessionAutosaveService::Status::SAVING) {
            break;
        }
    }
    assert(encodeObserved);
    assert(progress.saved());
    assertCurrentSessionNote(files, 87U);

    std::cout << "[PASS] encode advance is finite, measured and I/O-free\n";
}

void test_stale_save_waits_for_its_foreground_turn_before_unwind() {
    resetTestRoot();

    oc::impl::HostFileSystem filesystem(testRoot().string().c_str());
    auto files = makeProductFiles(filesystem);
    core::persistence::ProjectSessionStore store(files);
    core::persistence::ProjectSessionAutosaveService autosave(store, 100U);

    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state, "p027", 88U);
    state.markProjectMutated();
    const uint32_t dueAt = state.projectSessionSaveTimestampMs() + 100U;

    for (uint16_t turn = 0U; turn < 192U && !store.saveCurrentInProgress(); ++turn) {
        const auto progress = updateAutosave(files, autosave, state, dueAt + turn);
        assert(progress.status ==
               core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    }
    assert(store.saveCurrentInProgress());
    assert(!store.saveCurrentWriteSessionActive());

    // Advance PREPARE so cancellation has real filesystem-owned state to
    // unwind, while the autosave is still at a declared safe yield.
    const auto prepared = updateAutosave(files, autosave, state, dueAt + 193U);
    assert(prepared.status ==
           core::persistence::ProjectSessionAutosaveService::Status::SAVING);
    assert(store.saveCurrentStage() == core::persistence::ProjectSaveStage::ENCODE);
    core::persistence::ProductPersistenceJobSnapshot autosaveBeforePreemption{};
    assert(autosave.inspectPersistenceJob(autosaveBeforePreemption));
    assert(autosaveBeforePreemption.safeYield);
    assert(autosaveBeforePreemption.lastUsage.filesystemCalls > 0U);

    auto& jobs = files.persistenceJobs();
    auto admittedRecovery = jobs.admit({
        .owner = core::persistence::ProductPersistenceJobOwner::STORAGE_RECOVERY,
        .nowMs = dueAt + 194U,
        .quota = core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE,
    });
    assert(admittedRecovery);
    auto recovery = std::move(admittedRecovery.value());
    assert(jobs.depth() == 2U);

    assert(jobs.beginTurn(dueAt + 194U));
    assert(jobs.isActive(recovery));
    assert(jobs.prepareAdvance(
        recovery,
        core::persistence::PRODUCT_PERSISTENCE_QUOTA_PROMOTION_PHASE
    ));
    assert(jobs.claimAdvance(recovery, dueAt + 194U));
    assert(jobs.finishAdvance(recovery, {}, true));

    state.sequencer.setStepDataAt(0U, 89U, 100U, 75U);
    state.markProjectMutated();
    const auto pendingToken = state.projectSessionSaveToken();

    // The recovery job already consumed this pass. Observing staleness may
    // latch cancellation, but it must not touch the transaction or filesystem.
    const auto deferredCancel = autosave.update(state, dueAt + 194U);
    assert(deferredCancel.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(state.projectSessionSaveTokenMatches(pendingToken));
    assert(store.saveCurrentInProgress());
    assert(jobs.depth() == 2U);

    assert(jobs.complete(recovery));
    assert(jobs.depth() == 1U);
    assert(jobs.beginTurn(dueAt + 195U));

    const auto cancelled = autosave.update(state, dueAt + 195U);
    assert(cancelled.status ==
           core::persistence::ProjectSessionAutosaveService::Status::WAITING);
    assert(state.projectSessionSaveTokenMatches(pendingToken));
    assert(!store.saveCurrentInProgress());
    assert(jobs.depth() == 0U);
    assert(state.hasPendingProjectSessionSave());
    assertNoCurrentSessionFile();

    std::cout << "[PASS] stale save waits for its own foreground unwind turn\n";
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
    test_same_mutation_second_request_invalidates_in_flight_capture();
    test_equal_counter_project_replacement_cancels_stale_save();
    test_exact_acknowledgement_rejects_a_stale_completion();
    test_checked_token_rollover_rotates_session_identity();
    test_write_block_pauses_an_in_flight_capture();
    test_mutation_after_tmp_write_discards_the_stale_transaction();
    test_pending_live_edit_aborts_an_in_flight_write();
    test_modulator_audition_blocks_and_aborts_an_in_flight_autosave();
    test_flush_refuses_an_active_modulator_audition();
    test_flush_writes_without_waiting();
    test_destruction_cancels_an_in_flight_write();
    test_playback_blocks_admission_and_freezes_admitted_work();
    test_admitted_autosave_performs_no_extmem_allocation();
    test_stale_capture_after_durable_mapping_preserves_recovery_evidence();
    test_encode_advance_is_finite_measured_and_io_free();
    test_stale_save_waits_for_its_foreground_turn_before_unwind();

    resetTestRoot();

    std::cout << "\n====================================================\n";
    std::cout << "All tests passed\n";
    std::cout << "====================================================\n";
    return 0;
}

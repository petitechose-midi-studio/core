#ifdef NDEBUG
#undef NDEBUG
#endif

#include <array>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "../../src/app/ExtmemAllocator.hpp"
#include "../../src/persistence/ProjectMigration.hpp"
#include "../../src/persistence/ProjectSnapshotPersistenceCodec.hpp"
#include "../../src/persistence/SequencerPersistenceEnvelope.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../support/CoreStorages.hpp"

namespace {

namespace migration = core::persistence::project_file_migration;
namespace project = core::state::project;
namespace project_file = core::persistence::project_file;
namespace snapshot_codec = core::persistence::project_snapshot_codec;

constexpr size_t kProjectMigrationScratchSize = 512U * 1024U;

std::filesystem::path fixturePath(const char* relativePath) {
    const auto repoRoot = std::filesystem::path(__FILE__).parent_path().parent_path().parent_path();
    return repoRoot / relativePath;
}

std::vector<uint8_t> readFixture(const char* relativePath) {
    const auto path = fixturePath(relativePath);
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    assert(file);
    const auto size = file.tellg();
    assert(size > 0);
    std::vector<uint8_t> bytes(static_cast<size_t>(size));
    file.seekg(0, std::ios::beg);
    assert(file.read(reinterpret_cast<char*>(bytes.data()), size));
    return bytes;
}

core::state::CoreState makeCoreState(test_support::CoreStorages& storages) {
    return core::state::CoreState{
        storages.settings,
        storages.macroLibrary,
        storages.sequencerPatternLibrary,
        storages.sequencerSetLibrary,
    };
}

bool reportHas(const project_file::LoadReport& report, project_file::LoadCode code) {
    for (uint8_t i = 0; i < report.itemCount; ++i) {
        if (report.items[i].code == code) return true;
    }
    return false;
}

void configureProject(core::state::CoreState& state) {
    std::strncpy(state.project.metadata.id.data(), "p321", state.project.metadata.id.size() - 1);
    std::strncpy(state.project.metadata.name.data(), "p321", state.project.metadata.name.size() - 1);
    state.project.metadata.hasSavedIdentity = true;
    state.project.metadata.modifiedCounter = 321;
    state.sequencer.pattern.length.set(12);
    state.sequencer.pattern.toggle(0);
    state.sequencer.pattern.note[0] = 64;
    state.sequencer.pattern.velocity[0] = 100;
}

void test_inspects_current_project() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state);

    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));

    auto bytes = core::app::makeExtmemUnique<std::array<uint8_t, kProjectMigrationScratchSize>>();
    assert(bytes);
    const auto encoded = snapshot_codec::encodeProjectSnapshot(
        snapshot,
        bytes->data(),
        static_cast<uint32_t>(bytes->size())
    );
    assert(encoded.status == project_file::Status::OK);

    project_file::LoadReport report{};
    const auto inspected = migration::inspectProjectBytes(
        bytes->data(),
        encoded.bytesWritten,
        &report
    );
    assert(inspected.status == migration::Status::CURRENT);
    assert(inspected.loadStatus == project_file::LoadStatus::OK);
    assert(inspected.overwriteSafe);
    assert(report.ok());
    assert(std::strcmp(migration::statusName(inspected.status), "current") == 0);

    auto migratedBytes =
        core::app::makeExtmemUnique<std::array<uint8_t, kProjectMigrationScratchSize>>();
    assert(migratedBytes);
    project_file::LoadReport migrateReport{};
    const auto migrated = migration::migrateProjectBytesToCurrent(
        bytes->data(),
        encoded.bytesWritten,
        migratedBytes->data(),
        static_cast<uint32_t>(migratedBytes->size()),
        &migrateReport
    );
    assert(migrated.status == migration::Status::CURRENT);
    assert(migrated.bytesWritten > 0);
    assert(migrateReport.ok());

    std::cout << "[PASS] test_inspects_current_project\n";
}

void test_stale_sequencer_project_is_partial_and_not_rewritten_by_default() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state);

    auto envelope =
        core::app::makeExtmemUnique<core::persistence::sequencer_codec::EnvelopeBuffer>();
    assert(envelope);
    const auto encodedSequencer =
        core::persistence::sequencer_codec::fillProjectSequencerEnvelope(
            state.sequencerTracks,
            state.sequencer,
            envelope->bytes.data(),
            static_cast<uint16_t>(envelope->bytes.size())
        );
    assert(encodedSequencer.ok);

    const project_file::ChunkView chunks[] = {{
        .id = project_file::chunkIdValue(project_file::ChunkId::SEQUENCER_STATE),
        .versionMajor = snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MAJOR,
        .versionMinor = static_cast<uint8_t>(
            snapshot_codec::PROJECT_SNAPSHOT_CHUNK_VERSION_MINOR - 1U
        ),
        .flags = 0,
        .data = envelope->bytes.data(),
        .size = encodedSequencer.size,
    }};

    auto bytes = core::app::makeExtmemUnique<std::array<uint8_t, kProjectMigrationScratchSize>>();
    assert(bytes);
    const auto encoded = project_file::encode(chunks, 1, 0, bytes->data(), bytes->size());
    assert(encoded.status == project_file::Status::OK);

    project_file::LoadReport report{};
    const auto inspected = migration::inspectProjectBytes(
        bytes->data(),
        encoded.bytesWritten,
        &report
    );
    assert(inspected.status == migration::Status::PARTIAL);
    assert(inspected.loadStatus == project_file::LoadStatus::PARTIAL);
    assert(!inspected.overwriteSafe);
    assert(report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));

    auto migratedBytes =
        core::app::makeExtmemUnique<std::array<uint8_t, kProjectMigrationScratchSize>>();
    assert(migratedBytes);
    project_file::LoadReport migrateReport{};
    const auto migrated = migration::migrateProjectBytesToCurrent(
        bytes->data(),
        encoded.bytesWritten,
        migratedBytes->data(),
        static_cast<uint32_t>(migratedBytes->size()),
        &migrateReport
    );
    assert(migrated.status == migration::Status::PARTIAL);
    assert(migrated.bytesWritten == 0);
    assert(!migrated.overwriteSafe);

    project_file::LoadReport forcedReport{};
    const auto forced = migration::migrateProjectBytesToCurrent(
        bytes->data(),
        encoded.bytesWritten,
        migratedBytes->data(),
        static_cast<uint32_t>(migratedBytes->size()),
        &forcedReport,
        {.allowPartialOutput = true}
    );
    assert(forced.status == migration::Status::PARTIAL);
    assert(forced.bytesWritten > 0);
    assert(!forced.overwriteSafe);

    std::cout << "[PASS] test_stale_sequencer_project_is_partial_and_not_rewritten_by_default\n";
}

void test_stale_sequencer_fixture_is_partial() {
    const auto bytes = readFixture("test/fixtures/projects/v1_0/stale-sequencer.mspj");

    project_file::LoadReport report{};
    const auto inspected = migration::inspectProjectBytes(
        bytes.data(),
        static_cast<uint32_t>(bytes.size()),
        &report
    );

    assert(inspected.status == migration::Status::PARTIAL);
    assert(inspected.loadStatus == project_file::LoadStatus::PARTIAL);
    assert(!inspected.overwriteSafe);
    assert(report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));

    auto migratedBytes =
        core::app::makeExtmemUnique<std::array<uint8_t, kProjectMigrationScratchSize>>();
    assert(migratedBytes);
    project_file::LoadReport migrateReport{};
    const auto migrated = migration::migrateProjectBytesToCurrent(
        bytes.data(),
        static_cast<uint32_t>(bytes.size()),
        migratedBytes->data(),
        static_cast<uint32_t>(migratedBytes->size()),
        &migrateReport
    );
    assert(migrated.status == migration::Status::PARTIAL);
    assert(migrated.bytesWritten == 0);
    assert(!migrated.overwriteSafe);

    std::cout << "[PASS] test_stale_sequencer_fixture_is_partial\n";
}

void test_previous_current_fixture_is_partial_after_macro_payload_change() {
    const auto bytes = readFixture(
        "test/fixtures/projects/v1_1/current-from-stale-sequencer.mspj"
    );

    project_file::LoadReport report{};
    const auto inspected = migration::inspectProjectBytes(
        bytes.data(),
        static_cast<uint32_t>(bytes.size()),
        &report
    );

    assert(inspected.status == migration::Status::PARTIAL);
    assert(inspected.loadStatus == project_file::LoadStatus::PARTIAL);
    assert(!inspected.overwriteSafe);
    assert(!report.ok());
    assert(report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(reportHas(report, project_file::LoadCode::CHUNK_PAYLOAD_INVALID));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));

    std::cout << "[PASS] test_previous_current_fixture_is_partial_after_macro_payload_change\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectMigration tests\n";
    std::cout << "==============================================\n\n";

    test_inspects_current_project();
    test_stale_sequencer_project_is_partial_and_not_rewritten_by_default();
    test_stale_sequencer_fixture_is_partial();
    test_previous_current_fixture_is_partial_after_macro_payload_change();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

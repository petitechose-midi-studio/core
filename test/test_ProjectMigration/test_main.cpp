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
#include "../../src/persistence/ProjectControlPersistencePayloads.hpp"
#include "../../src/persistence/ProjectSnapshotPersistenceCodec.hpp"
#include "../../src/persistence/SequencerPersistenceEnvelope.hpp"
#include "../../src/state/CoreState.hpp"
#include "../../src/state/project/ProjectSnapshot.hpp"
#include "../../src/state/modulation/ProjectModulationDomainOps.hpp"
#include "../support/CoreStorages.hpp"
#include "../support/ProjectSequencerEnvelopeTestSupport.hpp"

namespace {

namespace migration = core::persistence::project_file_migration;
namespace project = core::state::project;
namespace project_file = core::persistence::project_file;
namespace snapshot_codec = core::persistence::project_snapshot_codec;
namespace control_codec = core::persistence::project_control_codec;
namespace modulation = core::state::modulation;

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

bool reportHasModgMigration(const project_file::LoadReport& report) {
    for (uint8_t i = 0; i < report.itemCount; ++i) {
        const auto& item = report.items[i];
        if (item.code == project_file::LoadCode::MIGRATED_CHUNK &&
            item.chunkId == project_file::chunkIdValue(
                project_file::ChunkId::MODULATION_GRAPH
            ) &&
            item.sourceMajor == control_codec::PROJECT_CONTROL_CHUNK_VERSION_MAJOR &&
            item.sourceMinor ==
                control_codec::PROJECT_MODULATION_GRAPH_LEGACY_VERSION_MINOR &&
            item.targetMinor ==
                control_codec::PROJECT_MODULATION_GRAPH_CHUNK_VERSION_MINOR) {
            return true;
        }
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

std::vector<uint8_t> buildModg10ApplicationFixture() {
    test_support::CoreStorages storages;
    auto state = makeCoreState(storages);
    configureProject(state);

    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));
    assert(snapshot.projectControl);
    auto& control = *snapshot.projectControl;
    modulation::ModulatorLfoDraft source{};
    source.name = "Compat LFO";
    source.reach.kind = modulation::ModulatorReachKind::PROJECT;
    const auto created = modulation::createLfoModulator(
        control.modulation,
        source
    );
    assert(created.changed());
    modulation::ModulationBindingDraft around{};
    around.sourceId = created.sourceId;
    around.destination = {
        modulation::ModulationDestinationKind::MACRO_SLOT,
        0U,
        0U,
        0U,
    };
    around.amountQ15 = 16384;
    around.application = modulation::ModulationApplication::AROUND_BASE;
    assert(modulation::addProjectModulationBinding(
        control.modulation,
        around
    ).changed());
    auto from = around;
    from.destination.macro = 1U;
    from.amountQ15 = -8192;
    from.application = modulation::ModulationApplication::FROM_BASE;
    assert(modulation::addProjectModulationBinding(
        control.modulation,
        from
    ).changed());

    auto current = core::app::makeExtmemUnique<
        std::array<uint8_t, kProjectMigrationScratchSize>
    >();
    assert(current);
    const auto encoded = snapshot_codec::encodeProjectSnapshot(
        snapshot,
        current->data(),
        static_cast<uint32_t>(current->size())
    );
    assert(encoded.status == project_file::Status::OK);

    std::array<
        project_file::DecodedChunkView,
        project_file::MAX_CHUNKS
    > decoded{};
    const auto container = project_file::decode(
        current->data(),
        encoded.bytesWritten,
        decoded.data(),
        static_cast<uint16_t>(decoded.size())
    );
    assert(container.status == project_file::Status::OK);

    std::vector<uint8_t> legacyGraph;
    for (uint16_t index = 0; index < container.chunkCount; ++index) {
        if (decoded[index].id == project_file::chunkIdValue(
                project_file::ChunkId::MODULATION_GRAPH
            )) {
            legacyGraph.assign(
                decoded[index].data,
                decoded[index].data + decoded[index].size
            );
            break;
        }
    }
    assert(!legacyGraph.empty());
    const uint16_t sourceCount = static_cast<uint16_t>(
        legacyGraph[0] | (static_cast<uint16_t>(legacyGraph[1]) << 8U)
    );
    const uint16_t bindingCount = static_cast<uint16_t>(
        legacyGraph[2] | (static_cast<uint16_t>(legacyGraph[3]) << 8U)
    );
    assert(sourceCount == 1U && bindingCount == 2U);
    const uint32_t bindingBase =
        control_codec::PROJECT_CONTROL_CHUNK_HEADER_SIZE +
        static_cast<uint32_t>(sourceCount) *
            control_codec::PROJECT_MODULATOR_SOURCE_DIRECTORY_SIZE +
        static_cast<uint32_t>(sourceCount) *
            control_codec::PROJECT_MODULATOR_SOURCE_PAYLOAD_SIZE;
    auto& aroundByte = legacyGraph[bindingBase + 14U];
    auto& fromByte = legacyGraph[
        bindingBase + control_codec::PROJECT_MODULATION_BINDING_SIZE + 14U
    ];
    assert(aroundByte == static_cast<uint8_t>(
        modulation::ModulationApplication::AROUND_BASE
    ));
    assert(fromByte == static_cast<uint8_t>(
        modulation::ModulationApplication::FROM_BASE
    ));
    aroundByte = 0U;
    fromByte = 1U;

    std::array<project_file::ChunkView, project_file::MAX_CHUNKS> chunks{};
    for (uint16_t index = 0; index < container.chunkCount; ++index) {
        const bool graph = decoded[index].id == project_file::chunkIdValue(
            project_file::ChunkId::MODULATION_GRAPH
        );
        chunks[index] = {
            .id = decoded[index].id,
            .versionMajor = decoded[index].versionMajor,
            .versionMinor = graph
                ? control_codec::PROJECT_MODULATION_GRAPH_LEGACY_VERSION_MINOR
                : decoded[index].versionMinor,
            .flags = decoded[index].flags,
            .data = graph ? legacyGraph.data() : decoded[index].data,
            .size = graph
                ? static_cast<uint32_t>(legacyGraph.size())
                : decoded[index].size,
        };
    }

    std::vector<uint8_t> output(kProjectMigrationScratchSize);
    const auto legacyEncoded = project_file::encode(
        chunks.data(),
        container.chunkCount,
        state.project.metadata.modifiedCounter,
        output.data(),
        static_cast<uint32_t>(output.size())
    );
    assert(legacyEncoded.status == project_file::Status::OK);
    output.resize(legacyEncoded.bytesWritten);
    return output;
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
    project::ProjectSnapshot snapshot;
    assert(project::captureProjectSnapshot(state, snapshot));
    const auto encodedSequencer =
        test_support::encodeProjectSequencerSnapshot(snapshot.sequencer, *envelope);
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

void test_rewritten_fixture_migrates_legacy_macro_lifecycle_payload() {
    const auto bytes = readFixture(
        "test/fixtures/projects/v1_1/current-from-stale-sequencer.mspj"
    );

    project_file::LoadReport report{};
    const auto inspected = migration::inspectProjectBytes(
        bytes.data(),
        static_cast<uint32_t>(bytes.size()),
        &report
    );

    assert(inspected.status == migration::Status::MIGRATED);
    assert(inspected.loadStatus == project_file::LoadStatus::MIGRATED);
    assert(inspected.overwriteSafe);
    assert(report.ok());
    assert(!report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::MIGRATED_CHUNK));

    std::cout << "[PASS] test_rewritten_fixture_migrates_legacy_macro_lifecycle_payload\n";
}

void test_modg10_application_fixture_migrates_losslessly_to_12() {
    const auto expected = buildModg10ApplicationFixture();
    const auto bytes = readFixture(
        "test/fixtures/projects/v1_0/modg-application-1.0.mspj"
    );
    assert(bytes == expected);

    project_file::LoadReport report{};
    const auto inspected = migration::inspectProjectBytes(
        bytes.data(),
        static_cast<uint32_t>(bytes.size()),
        &report
    );
    assert(inspected.status == migration::Status::MIGRATED);
    assert(inspected.loadStatus == project_file::LoadStatus::MIGRATED);
    assert(inspected.overwriteSafe);
    assert(reportHasModgMigration(report));

    project::ProjectSnapshot lifted;
    project_file::LoadReport liftedReport{};
    const auto decoded = migration::decodeProjectBytesToSnapshot(
        bytes.data(),
        static_cast<uint32_t>(bytes.size()),
        lifted,
        &liftedReport
    );
    assert(decoded.status == migration::Status::MIGRATED);
    assert(lifted.projectControl);
    assert(lifted.projectControl->modulation.outputBindingCount == 2U);
    assert(lifted.projectControl->modulation.outputBindings[0].application ==
           modulation::ModulationApplication::AROUND_BASE);
    assert(lifted.projectControl->modulation.outputBindings[1].application ==
           modulation::ModulationApplication::FROM_BASE);

    auto current = core::app::makeExtmemUnique<
        std::array<uint8_t, kProjectMigrationScratchSize>
    >();
    assert(current);
    project_file::LoadReport migrateReport{};
    const auto migrated = migration::migrateProjectBytesToCurrent(
        bytes.data(),
        static_cast<uint32_t>(bytes.size()),
        current->data(),
        static_cast<uint32_t>(current->size()),
        &migrateReport
    );
    assert(migrated.status == migration::Status::MIGRATED);
    assert(migrated.bytesWritten > 0U && migrated.overwriteSafe);

    project_file::LoadReport currentReport{};
    const auto currentInspection = migration::inspectProjectBytes(
        current->data(),
        migrated.bytesWritten,
        &currentReport
    );
    assert(currentInspection.status == migration::Status::CURRENT);
    assert(currentInspection.loadStatus == project_file::LoadStatus::OK);
    assert(currentInspection.overwriteSafe && currentReport.ok());

    std::cout << "[PASS] test_modg10_application_fixture_migrates_losslessly_to_12\n";
}

}  // namespace

int main(int argc, char** argv) {
    if (argc == 3 &&
        std::strcmp(argv[1], "--write-modg10-fixture") == 0) {
        const auto bytes = buildModg10ApplicationFixture();
        const std::filesystem::path path(argv[2]);
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        assert(file.write(
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size())
        ));
        return 0;
    }
    std::cout << "==============================================\n";
    std::cout << "ProjectMigration tests\n";
    std::cout << "==============================================\n\n";

    test_inspects_current_project();
    test_stale_sequencer_project_is_partial_and_not_rewritten_by_default();
    test_stale_sequencer_fixture_is_partial();
    test_rewritten_fixture_migrates_legacy_macro_lifecycle_payload();
    test_modg10_application_fixture_migrates_losslessly_to_12();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

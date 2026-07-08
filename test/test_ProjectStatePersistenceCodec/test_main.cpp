#include <cassert>
#include <array>
#include <cstring>
#include <iostream>

#include "../../src/persistence/PersistenceBinaryCodec.hpp"
#include "../../src/persistence/ProjectStatePersistenceCodec.hpp"
#include "../../src/state/project/ProjectSlug.hpp"

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;
namespace codec = core::persistence::project_state_codec;
namespace binary = core::persistence::binary_codec;

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleType;

constexpr uint32_t kTransportV0FixtureSize = 4;
constexpr uint32_t kProjectMetaV1_0FixtureSize = 48;

bool reportHas(const project_file::LoadReport& report, project_file::LoadCode code) {
    for (uint8_t i = 0; i < report.itemCount; ++i) {
        if (report.items[i].code == code) return true;
    }
    return false;
}

bool sameProjectCore(const project::ProjectState& lhs, const project::ProjectState& rhs) {
    if (std::strcmp(lhs.metadata.id.data(), rhs.metadata.id.data()) != 0) return false;
    if (std::strcmp(lhs.metadata.name.data(), rhs.metadata.name.data()) != 0) return false;
    if (lhs.metadata.modifiedCounter != rhs.metadata.modifiedCounter) return false;
    if (lhs.metadata.dirty != rhs.metadata.dirty) return false;
    if (lhs.metadata.hasSavedIdentity != rhs.metadata.hasSavedIdentity) return false;
    if (lhs.transport.tempoBpm != rhs.transport.tempoBpm) return false;
    if (lhs.transport.swingPercent != rhs.transport.swingPercent) return false;
    if (lhs.transport.runMode != rhs.transport.runMode) return false;
    if (lhs.musical.scale.root != rhs.musical.scale.root) return false;
    if (lhs.musical.scale.type != rhs.musical.scale.type) return false;
    if (lhs.musical.scale.mode != rhs.musical.scale.mode) return false;
    if (lhs.musical.patternsInheritScale != rhs.musical.patternsInheritScale) return false;
    if (lhs.musical.clipsInheritScale != rhs.musical.clipsInheritScale) return false;
    if (lhs.editing.stepPasteMode != rhs.editing.stepPasteMode) return false;
    for (uint8_t i = 0; i < lhs.routing.outputMidiChannels.size(); ++i) {
        if (lhs.routing.outputMidiChannels[i] != rhs.routing.outputMidiChannels[i]) {
            return false;
        }
    }
    return true;
}

project::ProjectState makeProject() {
    project::ProjectState state;
    std::strncpy(state.metadata.id.data(), "p042", state.metadata.id.size() - 1);
    std::strncpy(state.metadata.name.data(), "p042", state.metadata.name.size() - 1);
    state.metadata.modifiedCounter = 1234;
    state.metadata.dirty = true;
    state.metadata.hasSavedIdentity = true;
    state.transport.tempoBpm = 137.25f;
    state.transport.swingPercent = 31;
    state.transport.runMode = 2;
    state.musical.scale = {
        .root = 6,
        .type = StepSequencerScaleType::HarmonicMinor,
        .mode = StepSequencerScaleConstraintMode::ConstrainNearest,
    };
    state.musical.patternsInheritScale = false;
    state.musical.clipsInheritScale = true;
    state.editing.stepPasteMode = project::ProjectStepPasteMode::WRAP;
    for (uint8_t i = 0; i < state.routing.outputMidiChannels.size(); ++i) {
        state.routing.outputMidiChannels[i] = static_cast<uint8_t>(15U - i);
    }
    return state;
}

std::array<uint8_t, kTransportV0FixtureSize> makeTransportV0Fixture(uint16_t tempoBpm,
                                                                    uint8_t swingPercent) {
    std::array<uint8_t, kTransportV0FixtureSize> bytes{};
    binary::Writer writer(bytes.data(), static_cast<uint32_t>(bytes.size()));
    assert(writer.writeU16(tempoBpm));
    assert(writer.writeU8(swingPercent));
    assert(writer.writeU8(0));
    assert(writer.ok());
    assert(writer.offset() == bytes.size());
    return bytes;
}

std::array<uint8_t, kProjectMetaV1_0FixtureSize> makeProjectMetaV1_0Fixture(
    const char* id,
    const char* name,
    uint32_t modifiedCounter,
    uint8_t flags
) {
    std::array<uint8_t, kProjectMetaV1_0FixtureSize> bytes{};
    char idBytes[16] = {};
    char nameBytes[24] = {};
    std::strncpy(idBytes, id, sizeof(idBytes) - 1U);
    std::strncpy(nameBytes, name, sizeof(nameBytes) - 1U);

    binary::Writer writer(bytes.data(), static_cast<uint32_t>(bytes.size()));
    assert(writer.writeBytes(idBytes, sizeof(idBytes)));
    assert(writer.writeBytes(nameBytes, sizeof(nameBytes)));
    assert(writer.writeU32(modifiedCounter));
    assert(writer.writeU8(flags));
    assert(writer.writeU8(0));
    assert(writer.writeU16(0));
    assert(writer.ok());
    assert(writer.offset() == bytes.size());
    return bytes;
}

void test_project_state_roundtrip_core_chunks() {
    auto source = makeProject();

    uint8_t bytes[512] = {};
    auto encodeResult = codec::encodeProjectState(source, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    project_file::LoadReport report{};
    auto decodeResult = codec::decodeProjectState(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.containerStatus == project_file::Status::OK);
    assert(decodeResult.loadStatus == project_file::LoadStatus::OK);
    assert(decodeResult.overwriteSafe);
    assert(report.ok());
    assert(sameProjectCore(source, loaded));

    std::cout << "[PASS] test_project_state_roundtrip_core_chunks\n";
}

void test_project_state_roundtrip_long_slug() {
    auto source = makeProject();
    source.metadata.id.fill('\0');
    source.metadata.name.fill('\0');
    for (size_t i = 0; i < project::PROJECT_SLUG_MAX_LENGTH; ++i) {
        source.metadata.id[i] = 'a';
        source.metadata.name[i] = 'a';
    }

    uint8_t bytes[768] = {};
    auto encodeResult = codec::encodeProjectState(source, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    project_file::LoadReport report{};
    auto decodeResult = codec::decodeProjectState(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.loadStatus == project_file::LoadStatus::OK);
    assert(decodeResult.overwriteSafe);
    assert(report.ok());
    assert(sameProjectCore(source, loaded));

    std::cout << "[PASS] test_project_state_roundtrip_long_slug\n";
}

void test_missing_optional_chunks_default_and_report_without_blocking_overwrite() {
    uint8_t bytes[128] = {};
    auto encodeResult = project_file::encode(nullptr, 0, 0, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    project_file::LoadReport report{};
    auto decodeResult = codec::decodeProjectState(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.overwriteSafe);
    assert(report.ok());
    assert(reportHas(report, project_file::LoadCode::MISSING_OPTIONAL_CHUNK));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));

    project::ProjectState defaults;
    assert(sameProjectCore(loaded, defaults));

    std::cout << "[PASS] test_missing_optional_chunks_default_and_report_without_blocking_overwrite\n";
}

void test_transport_v0_chunk_migrates_to_current_payload() {
    const auto transport = makeTransportV0Fixture(142, 12);

    const project_file::ChunkView chunks[] = {{
        .id = project_file::chunkIdValue(project_file::ChunkId::TRANSPORT),
        .versionMajor = 0,
        .versionMinor = 0,
        .flags = 0,
        .data = transport.data(),
        .size = static_cast<uint32_t>(transport.size()),
    }};

    uint8_t bytes[160] = {};
    auto encodeResult = project_file::encode(chunks, 1, 0, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    project_file::LoadReport report{};
    auto decodeResult = codec::decodeProjectState(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.overwriteSafe);
    assert(report.status == project_file::LoadStatus::MIGRATED);
    assert(!report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::MIGRATED_CHUNK));
    assert(loaded.transport.tempoBpm == 142.0f);
    assert(loaded.transport.swingPercent == 12);
    assert(loaded.transport.runMode == project::ProjectTransportState::DEFAULT_RUN_MODE);

    std::cout << "[PASS] test_transport_v0_chunk_migrates_to_current_payload\n";
}

void test_project_meta_v1_0_chunk_migrates_generated_id_to_slug() {
    const auto meta = makeProjectMetaV1_0Fixture("P042", "Project 042", 42, 0x03);

    const project_file::ChunkView chunks[] = {{
        .id = project_file::chunkIdValue(project_file::ChunkId::PROJECT_META),
        .versionMajor = codec::PROJECT_STATE_CHUNK_VERSION_MAJOR,
        .versionMinor = 0,
        .flags = 0,
        .data = meta.data(),
        .size = static_cast<uint32_t>(meta.size()),
    }};

    uint8_t bytes[256] = {};
    auto encodeResult = project_file::encode(chunks, 1, 0, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    project_file::LoadReport report{};
    auto decodeResult = codec::decodeProjectState(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(decodeResult.ok);
    assert(decodeResult.overwriteSafe);
    assert(report.status == project_file::LoadStatus::MIGRATED);
    assert(reportHas(report, project_file::LoadCode::MIGRATED_CHUNK));
    assert(std::strcmp(loaded.metadata.id.data(), "p042") == 0);
    assert(std::strcmp(loaded.metadata.name.data(), "p042") == 0);
    assert(loaded.metadata.modifiedCounter == 42);
    assert(loaded.metadata.dirty);
    assert(loaded.metadata.hasSavedIdentity);

    std::cout << "[PASS] test_project_meta_v1_0_chunk_migrates_generated_id_to_slug\n";
}

void test_future_chunk_version_defaults_and_blocks_overwrite() {
    codec::ProjectTransportPayload transport{};
    transport.tempoCentiBpm = 18000;
    std::array<uint8_t, codec::PROJECT_TRANSPORT_PAYLOAD_SIZE> transportBytes{};
    assert(codec::encodeTransportPayload(
        transport,
        transportBytes.data(),
        static_cast<uint32_t>(transportBytes.size())
    ));
    const project_file::ChunkView chunks[] = {{
        .id = project_file::chunkIdValue(project_file::ChunkId::TRANSPORT),
        .versionMajor = static_cast<uint8_t>(codec::PROJECT_STATE_CHUNK_VERSION_MAJOR + 1),
        .versionMinor = 0,
        .flags = 0,
        .data = transportBytes.data(),
        .size = codec::PROJECT_TRANSPORT_PAYLOAD_SIZE,
    }};

    uint8_t bytes[160] = {};
    auto encodeResult = project_file::encode(chunks, 1, 0, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    project_file::LoadReport report{};
    auto decodeResult = codec::decodeProjectState(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(decodeResult.ok);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == project_file::LoadStatus::PARTIAL);
    assert(report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));
    assert(loaded.transport.tempoBpm == project::ProjectTransportState::DEFAULT_TEMPO_BPM);

    std::cout << "[PASS] test_future_chunk_version_defaults_and_blocks_overwrite\n";
}

void test_invalid_payload_size_defaults_and_reports_partial_load() {
    const uint8_t badTransport[] = {1, 2, 3};
    const project_file::ChunkView chunks[] = {{
        .id = project_file::chunkIdValue(project_file::ChunkId::TRANSPORT),
        .versionMajor = codec::PROJECT_STATE_CHUNK_VERSION_MAJOR,
        .versionMinor = codec::PROJECT_STATE_CHUNK_VERSION_MINOR,
        .flags = 0,
        .data = badTransport,
        .size = sizeof(badTransport),
    }};

    uint8_t bytes[160] = {};
    auto encodeResult = project_file::encode(chunks, 1, 0, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    project_file::LoadReport report{};
    auto decodeResult = codec::decodeProjectState(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(decodeResult.ok);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == project_file::LoadStatus::PARTIAL);
    assert(reportHas(report, project_file::LoadCode::CHUNK_PAYLOAD_INVALID));
    assert(reportHas(report, project_file::LoadCode::DEFAULTED_CHUNK));
    assert(loaded.transport.tempoBpm == project::ProjectTransportState::DEFAULT_TEMPO_BPM);

    std::cout << "[PASS] test_invalid_payload_size_defaults_and_reports_partial_load\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectStatePersistenceCodec tests\n";
    std::cout << "==============================================\n\n";

    test_project_state_roundtrip_core_chunks();
    test_project_state_roundtrip_long_slug();
    test_missing_optional_chunks_default_and_report_without_blocking_overwrite();
    test_transport_v0_chunk_migrates_to_current_payload();
    test_project_meta_v1_0_chunk_migrates_generated_id_to_slug();
    test_future_chunk_version_defaults_and_blocks_overwrite();
    test_invalid_payload_size_defaults_and_reports_partial_load();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

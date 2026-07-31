#include <cassert>
#include <array>
#include <cstring>
#include <iostream>

#include "../../src/persistence/PersistenceBinaryCodec.hpp"
#include "../../src/persistence/ProjectStatePersistenceCodec.hpp"
#include "../../src/state/project/ProjectSlug.hpp"
#include "support/ProjectStatePersistenceTestSupport.hpp"

namespace {

namespace project = core::state::project;
namespace project_file = core::persistence::project_file;
namespace codec = core::persistence::project_state_codec;
namespace container_support = core::test::project_state_persistence;
namespace binary = core::persistence::binary_codec;

using oc::note::sequencer::StepSequencerScaleConstraintMode;
using oc::note::sequencer::StepSequencerScaleType;

constexpr uint32_t kTransportV0FixtureSize = 4;
constexpr uint32_t kProjectMetaV1_0FixtureSize = 48;
static_assert(codec::PROJECT_STATE_CHUNK_VERSION_MINOR > 0,
              "Same-size stale-minor test needs a previous minor version");

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
    if (lhs.editing.ccLaneDefaultControllers !=
        rhs.editing.ccLaneDefaultControllers) return false;
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
    state.editing.ccLaneDefaultControllers = {0U, 22U, 99U, 127U};
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
    auto encodeResult = container_support::encode(source, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    project_file::LoadReport report{};
    auto decodeResult = container_support::decode(
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
    auto encodeResult = container_support::encode(source, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    project_file::LoadReport report{};
    auto decodeResult = container_support::decode(
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

void test_missing_required_chunks_are_rejected_atomically() {
    uint8_t bytes[128] = {};
    auto encodeResult = project_file::encode(nullptr, 0, 0, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    loaded.transport.tempoBpm = 91.25F;
    project_file::LoadReport report{};
    auto decodeResult = container_support::decode(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(!decodeResult.ok);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == project_file::LoadStatus::FAILED);
    assert(reportHas(report, project_file::LoadCode::MISSING_REQUIRED_CHUNK));
    assert(loaded.transport.tempoBpm == 91.25F);

    std::cout << "[PASS] missing required chunks are rejected atomically\n";
}

void test_same_size_stale_minor_is_rejected_atomically() {
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
        .versionMajor = codec::PROJECT_STATE_CHUNK_VERSION_MAJOR,
        .versionMinor = static_cast<uint8_t>(codec::PROJECT_STATE_CHUNK_VERSION_MINOR - 1U),
        .flags = 0,
        .data = transportBytes.data(),
        .size = codec::PROJECT_TRANSPORT_PAYLOAD_SIZE,
    }};

    uint8_t bytes[160] = {};
    auto encodeResult = project_file::encode(chunks, 1, 0, bytes, sizeof(bytes));
    assert(encodeResult.status == project_file::Status::OK);

    project::ProjectState loaded;
    loaded.transport.tempoBpm = 91.25F;
    project_file::LoadReport report{};
    auto decodeResult = container_support::decode(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(!decodeResult.ok);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == project_file::LoadStatus::FAILED);
    assert(reportHas(report, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(loaded.transport.tempoBpm == 91.25F);

    std::cout << "[PASS] stale minor is rejected atomically\n";
}

void test_future_chunk_version_is_rejected_atomically() {
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
    loaded.transport.tempoBpm = 91.25F;
    project_file::LoadReport report{};
    auto decodeResult = container_support::decode(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(!decodeResult.ok);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == project_file::LoadStatus::FAILED);
    assert(report.hasUnknownUnsupportedData);
    assert(reportHas(report, project_file::LoadCode::UNSUPPORTED_CHUNK_VERSION));
    assert(loaded.transport.tempoBpm == 91.25F);

    std::cout << "[PASS] future chunk version is rejected atomically\n";
}

void test_invalid_payload_size_is_rejected_atomically() {
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
    loaded.transport.tempoBpm = 91.25F;
    project_file::LoadReport report{};
    auto decodeResult = container_support::decode(
        bytes,
        encodeResult.bytesWritten,
        loaded,
        &report
    );
    assert(!decodeResult.ok);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == project_file::LoadStatus::FAILED);
    assert(reportHas(report, project_file::LoadCode::CHUNK_PAYLOAD_INVALID));
    assert(loaded.transport.tempoBpm == 91.25F);

    std::cout << "[PASS] invalid payload size is rejected atomically\n";
}

void test_editing_defaults_roundtrip_explicit_cc_zero_without_growth() {
    static_assert(codec::PROJECT_EDITING_PAYLOAD_SIZE == 8U);
    codec::ProjectEditingPayload source{};
    source.stepPasteMode = static_cast<uint8_t>(project::ProjectStepPasteMode::PAGE);
    source.ccLaneDefaultControllers[0] = 0U;
    source.ccLaneDefaultControllers[1] = 17U;
    source.ccLaneDefaultControllers[2] = 96U;
    source.ccLaneDefaultControllers[3] = 127U;

    std::array<uint8_t, codec::PROJECT_EDITING_PAYLOAD_SIZE> bytes{};
    assert(codec::encodeEditingPayload(source, bytes.data(), bytes.size()));
    assert(bytes[1] == codec::PROJECT_EDITING_CC_LANE_DEFAULTS_MARKER);
    assert(bytes[2] == 0U);
    assert(bytes[6] == 0U && bytes[7] == 0U);

    codec::ProjectEditingPayload decoded{};
    assert(codec::decodeEditingPayload(bytes.data(), bytes.size(), decoded));
    assert(decoded.stepPasteMode == source.stepPasteMode);
    for (uint8_t lane = 0; lane < project::PROJECT_CC_LANE_DEFAULT_COUNT; ++lane) {
        assert(decoded.ccLaneDefaultControllers[lane] ==
               source.ccLaneDefaultControllers[lane]);
    }

    std::cout << "[PASS] editing defaults roundtrip explicit CC0 in unchanged payload\n";
}

void test_editing_corruption_is_rejected_transactionally() {
    const codec::ProjectEditingPayload sentinel = [] {
        codec::ProjectEditingPayload value{};
        value.ccLaneDefaultControllers[0] = 9U;
        return value;
    }();
    const auto expectRejected = [&](std::array<uint8_t, 8> bytes) {
        auto output = sentinel;
        assert(!codec::decodeEditingPayload(bytes.data(), bytes.size(), output));
        assert(output.ccLaneDefaultControllers[0] == 9U);
        assert(output.ccLaneDefaultsMarker == sentinel.ccLaneDefaultsMarker);
    };

    std::array<uint8_t, 8> invalidMarker{
        0U, 0x55U, 1U, 11U, 74U, 71U, 0U, 0U
    };
    expectRejected(invalidMarker);

    std::array<uint8_t, 8> invalidController{
        0U, codec::PROJECT_EDITING_CC_LANE_DEFAULTS_MARKER,
        1U, 11U, 128U, 71U, 0U, 0U
    };
    expectRejected(invalidController);

    std::array<uint8_t, 8> invalidReserved{
        0U, codec::PROJECT_EDITING_CC_LANE_DEFAULTS_MARKER,
        1U, 11U, 74U, 71U, 1U, 0U
    };
    expectRejected(invalidReserved);

    std::array<uint8_t, 8> invalidMarkerPayload{
        0U, 0U, 0U, 0U, 1U, 0U, 0U, 0U
    };
    expectRejected(invalidMarkerPayload);

    std::cout << "[PASS] marker/controller/reserved corruption rejects transactionally\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectStatePersistenceCodec tests\n";
    std::cout << "==============================================\n\n";

    test_project_state_roundtrip_core_chunks();
    test_project_state_roundtrip_long_slug();
    test_missing_required_chunks_are_rejected_atomically();
    test_same_size_stale_minor_is_rejected_atomically();
    test_future_chunk_version_is_rejected_atomically();
    test_invalid_payload_size_is_rejected_atomically();
    test_editing_defaults_roundtrip_explicit_cc_zero_without_growth();
    test_editing_corruption_is_rejected_transactionally();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

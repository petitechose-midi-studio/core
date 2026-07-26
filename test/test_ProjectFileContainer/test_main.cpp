#include <cassert>
#include <cstdint>
#include <cstring>
#include <iostream>

#include "../../src/persistence/ProjectFileContainer.hpp"

namespace {

using namespace core::persistence::project_file;

struct RawHeaderView {
    uint32_t magic = 0;
    uint8_t versionMajor = 0;
    uint8_t versionMinor = 0;
    uint16_t headerSize = 0;
    uint16_t chunkCount = 0;
    uint16_t directoryEntrySize = 0;
    uint32_t directoryOffset = 0;
    uint32_t payloadOffset = 0;
    uint32_t modifiedCounter = 0;
    uint32_t reserved0 = 0;
};

static_assert(sizeof(RawHeaderView) == 28, "Unexpected raw header view size");

bool reportHas(const LoadReport& report, LoadCode code) {
    for (uint8_t i = 0; i < report.itemCount; ++i) {
        if (report.items[i].code == code) return true;
    }
    return false;
}

uint32_t firstChunkPayloadOffset(const uint8_t* bytes) {
    RawHeaderView header{};
    std::memcpy(&header, bytes, sizeof(header));
    return header.payloadOffset;
}

void test_roundtrip_known_chunks() {
    const uint8_t meta[] = {'P', '0', '0', '1'};
    const uint8_t transport[] = {0x00, 0x00, 0xF0, 0x42};
    const ChunkView chunks[] = {
        {
            .id = chunkIdValue(ChunkId::PROJECT_META),
            .versionMajor = 1,
            .versionMinor = 0,
            .flags = 0,
            .data = meta,
            .size = sizeof(meta),
        },
        {
            .id = chunkIdValue(ChunkId::TRANSPORT),
            .versionMajor = 1,
            .versionMinor = 2,
            .flags = 7,
            .data = transport,
            .size = sizeof(transport),
        },
    };

    uint8_t encoded[160] = {};
    auto encodeResult = encode(chunks, 2, 42, encoded, sizeof(encoded));
    assert(encodeResult.status == Status::OK);
    assert(encodeResult.bytesWritten == encodedSize(chunks, 2));

    DecodedChunkView decoded[4] = {};
    LoadReport report{};
    auto decodeResult =
        decode(encoded, encodeResult.bytesWritten, decoded, 4, &report);
    assert(decodeResult.status == Status::OK);
    assert(decodeResult.chunkCount == 2);
    assert(decodeResult.overwriteSafe);
    assert(report.ok());
    assert(report.overwriteSafe);

    assert(decoded[0].known);
    assert(decoded[0].id == chunkIdValue(ChunkId::PROJECT_META));
    assert(decoded[0].size == sizeof(meta));
    assert(std::memcmp(decoded[0].data, meta, sizeof(meta)) == 0);

    assert(decoded[1].known);
    assert(decoded[1].id == chunkIdValue(ChunkId::TRANSPORT));
    assert(decoded[1].versionMinor == 2);
    assert(decoded[1].flags == 7);
    assert(std::memcmp(decoded[1].data, transport, sizeof(transport)) == 0);

    std::cout << "[PASS] test_roundtrip_known_chunks\n";
}

void test_unknown_chunk_is_preserved_as_read_view_and_blocks_overwrite() {
    const uint8_t payload[] = {1, 2, 3};
    const ChunkView chunks[] = {
        {
            .id = 0x46555452u,  // "FUTR"
            .versionMajor = 9,
            .versionMinor = 1,
            .flags = 0x10,
            .data = payload,
            .size = sizeof(payload),
        },
    };

    uint8_t encoded[128] = {};
    auto encodeResult = encode(chunks, 1, 1, encoded, sizeof(encoded));
    assert(encodeResult.status == Status::OK);

    DecodedChunkView decoded[2] = {};
    LoadReport report{};
    auto decodeResult =
        decode(encoded, encodeResult.bytesWritten, decoded, 2, &report);
    assert(decodeResult.status == Status::OK);
    assert(decodeResult.chunkCount == 1);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == LoadStatus::PARTIAL);
    assert(report.hasUnknownUnsupportedData);
    assert(!report.overwriteSafe);
    assert(reportHas(report, LoadCode::UNKNOWN_CHUNK));
    assert(!decoded[0].known);
    assert(decoded[0].id == 0x46555452u);

    std::cout << "[PASS] test_unknown_chunk_is_preserved_as_read_view_and_blocks_overwrite\n";
}

void test_crc_mismatch_skips_corrupt_chunk() {
    const uint8_t payload[] = {10, 20, 30, 40};
    const ChunkView chunks[] = {
        {
            .id = chunkIdValue(ChunkId::MUSICAL_CONTEXT),
            .versionMajor = 1,
            .versionMinor = 0,
            .flags = 0,
            .data = payload,
            .size = sizeof(payload),
        },
    };

    uint8_t encoded[128] = {};
    auto encodeResult = encode(chunks, 1, 3, encoded, sizeof(encoded));
    assert(encodeResult.status == Status::OK);
    encoded[firstChunkPayloadOffset(encoded)] ^= 0xFF;

    DecodedChunkView decoded[2] = {};
    LoadReport report{};
    auto decodeResult =
        decode(encoded, encodeResult.bytesWritten, decoded, 2, &report);
    assert(decodeResult.status == Status::OK);
    assert(decodeResult.chunkCount == 0);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == LoadStatus::PARTIAL);
    assert(reportHas(report, LoadCode::CHUNK_CRC_MISMATCH));

    std::cout << "[PASS] test_crc_mismatch_skips_corrupt_chunk\n";
}

void test_invalid_magic_fails() {
    uint8_t encoded[128] = {};
    const uint8_t payload[] = {1};
    const ChunkView chunks[] = {{
        .id = chunkIdValue(ChunkId::PROJECT_META),
        .versionMajor = 1,
        .versionMinor = 0,
        .flags = 0,
        .data = payload,
        .size = sizeof(payload),
    }};
    auto encodeResult = encode(chunks, 1, 1, encoded, sizeof(encoded));
    assert(encodeResult.status == Status::OK);
    encoded[0] = 0;

    DecodedChunkView decoded[2] = {};
    LoadReport report{};
    auto decodeResult =
        decode(encoded, encodeResult.bytesWritten, decoded, 2, &report);
    assert(decodeResult.status == Status::INVALID_CONTAINER);
    assert(decodeResult.chunkCount == 0);
    assert(report.status == LoadStatus::FAILED);
    assert(reportHas(report, LoadCode::INVALID_MAGIC));

    std::cout << "[PASS] test_invalid_magic_fails\n";
}

void test_future_container_major_decodes_best_effort_but_blocks_overwrite() {
    uint8_t encoded[128] = {};
    const uint8_t payload[] = {7, 8};
    const ChunkView chunks[] = {{
        .id = chunkIdValue(ChunkId::EDITING),
        .versionMajor = 1,
        .versionMinor = 0,
        .flags = 0,
        .data = payload,
        .size = sizeof(payload),
    }};
    auto encodeResult = encode(chunks, 1, 5, encoded, sizeof(encoded));
    assert(encodeResult.status == Status::OK);

    RawHeaderView header{};
    std::memcpy(&header, encoded, sizeof(header));
    header.versionMajor = static_cast<uint8_t>(CONTAINER_VERSION_MAJOR + 1);
    std::memcpy(encoded, &header, sizeof(header));

    DecodedChunkView decoded[2] = {};
    LoadReport report{};
    auto decodeResult =
        decode(encoded, encodeResult.bytesWritten, decoded, 2, &report);
    assert(decodeResult.status == Status::OK);
    assert(decodeResult.chunkCount == 1);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == LoadStatus::PARTIAL);
    assert(report.hasUnknownUnsupportedData);
    assert(reportHas(report, LoadCode::UNSUPPORTED_CONTAINER_VERSION));
    assert(decoded[0].id == chunkIdValue(ChunkId::EDITING));

    std::cout << "[PASS] test_future_container_major_decodes_best_effort_but_blocks_overwrite\n";
}

void test_encode_reports_required_size_when_buffer_too_small() {
    const uint8_t payload[] = {1, 2, 3, 4};
    const ChunkView chunks[] = {{
        .id = chunkIdValue(ChunkId::PROJECT_META),
        .versionMajor = 1,
        .versionMinor = 0,
        .flags = 0,
        .data = payload,
        .size = sizeof(payload),
    }};

    uint8_t encoded[16] = {};
    auto encodeResult = encode(chunks, 1, 1, encoded, sizeof(encoded));
    assert(encodeResult.status == Status::BUFFER_TOO_SMALL);
    assert(encodeResult.bytesWritten == encodedSize(chunks, 1));

    std::cout << "[PASS] test_encode_reports_required_size_when_buffer_too_small\n";
}

}  // namespace

int main() {
    std::cout << "==============================================\n";
    std::cout << "ProjectFileContainer tests\n";
    std::cout << "==============================================\n\n";

    test_roundtrip_known_chunks();
    test_unknown_chunk_is_preserved_as_read_view_and_blocks_overwrite();
    test_crc_mismatch_skips_corrupt_chunk();
    test_invalid_magic_fails();
    test_future_container_major_decodes_best_effort_but_blocks_overwrite();
    test_encode_reports_required_size_when_buffer_too_small();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

#include <cassert>
#include <array>
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

struct RawDirectoryEntryView {
    uint32_t id = 0;
    uint8_t versionMajor = 0;
    uint8_t versionMinor = 0;
    uint16_t flags = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t crc32 = 0;
};

static_assert(sizeof(RawHeaderView) == 28, "Unexpected raw header view size");
static_assert(sizeof(RawDirectoryEntryView) == 20, "Unexpected raw directory entry size");

using ContainerBytes = std::array<uint8_t, 192>;

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

RawHeaderView readHeader(const ContainerBytes& bytes) {
    RawHeaderView header{};
    std::memcpy(&header, bytes.data(), sizeof(header));
    return header;
}

void writeHeader(ContainerBytes& bytes, const RawHeaderView& header) {
    std::memcpy(bytes.data(), &header, sizeof(header));
}

RawDirectoryEntryView readDirectoryEntry(const ContainerBytes& bytes,
                                         const RawHeaderView& header,
                                         uint16_t index) {
    RawDirectoryEntryView entry{};
    const uint32_t offset =
        header.directoryOffset + static_cast<uint32_t>(index) * sizeof(entry);
    std::memcpy(&entry, bytes.data() + offset, sizeof(entry));
    return entry;
}

void writeDirectoryEntry(ContainerBytes& bytes,
                         const RawHeaderView& header,
                         uint16_t index,
                         const RawDirectoryEntryView& entry) {
    const uint32_t offset =
        header.directoryOffset + static_cast<uint32_t>(index) * sizeof(entry);
    std::memcpy(bytes.data() + offset, &entry, sizeof(entry));
}

void insertAnonymousByte(ContainerBytes& bytes, uint32_t& size, uint32_t offset) {
    assert(offset <= size);
    assert(size < bytes.size());
    std::memmove(bytes.data() + offset + 1U, bytes.data() + offset, size - offset);
    bytes[offset] = 0xA5U;
    ++size;
}

uint32_t encodeCanonicalTwoChunkFixture(ContainerBytes& bytes) {
    const uint8_t first[] = {1U, 2U, 3U, 4U};
    const uint8_t second[] = {3U, 4U};
    const ChunkView chunks[] = {
        {
            .id = chunkIdValue(ChunkId::PROJECT_META),
            .versionMajor = 1U,
            .versionMinor = 0U,
            .flags = 0U,
            .data = first,
            .size = sizeof(first),
        },
        {
            .id = chunkIdValue(ChunkId::TRANSPORT),
            .versionMajor = 1U,
            .versionMinor = 0U,
            .flags = 0U,
            .data = second,
            .size = sizeof(second),
        },
    };
    const auto encoded = encode(
        chunks,
        static_cast<uint16_t>(std::size(chunks)),
        1U,
        bytes.data(),
        static_cast<uint32_t>(bytes.size())
    );
    assert(encoded.status == Status::OK);
    return encoded.bytesWritten;
}

void assertNonCanonicalLayoutRejected(const ContainerBytes& bytes, uint32_t size) {
    DecodedChunkView decoded[4] = {};
    LoadReport report{};
    const auto result = scan(bytes.data(), size, decoded, 4U, &report);
    assert(result.status == Status::INVALID_CONTAINER);
    assert(result.chunkCount == 0U);
    assert(!result.overwriteSafe);
    assert(report.failed());
    assert(!report.overwriteSafe);
    assert(reportHas(report, LoadCode::CHUNK_DIRECTORY_INVALID));
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
        scan(encoded, encodeResult.bytesWritten, decoded, 4, &report);
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
        scan(encoded, encodeResult.bytesWritten, decoded, 2, &report);
    assert(decodeResult.status == Status::OK);
    assert(decodeResult.chunkCount == 1);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == LoadStatus::INSPECTION_ISSUES);
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
        scan(encoded, encodeResult.bytesWritten, decoded, 2, &report);
    assert(decodeResult.status == Status::OK);
    assert(decodeResult.chunkCount == 0);
    assert(!decodeResult.overwriteSafe);
    assert(report.status == LoadStatus::INSPECTION_ISSUES);
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
        scan(encoded, encodeResult.bytesWritten, decoded, 2, &report);
    assert(decodeResult.status == Status::INVALID_CONTAINER);
    assert(decodeResult.chunkCount == 0);
    assert(report.status == LoadStatus::FAILED);
    assert(reportHas(report, LoadCode::INVALID_MAGIC));

    std::cout << "[PASS] test_invalid_magic_fails\n";
}

void test_noncurrent_container_versions_can_be_scanned_for_inspection_only() {
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

    const uint8_t versions[][2] = {
        {0U, 0U},
        {CONTAINER_VERSION_MAJOR, static_cast<uint8_t>(CONTAINER_VERSION_MINOR + 1U)},
        {static_cast<uint8_t>(CONTAINER_VERSION_MAJOR + 1U), 0U},
    };
    for (const auto& version : versions) {
        uint8_t candidate[sizeof(encoded)]{};
        std::memcpy(candidate, encoded, sizeof(candidate));
        RawHeaderView header{};
        std::memcpy(&header, candidate, sizeof(header));
        header.versionMajor = version[0];
        header.versionMinor = version[1];
        std::memcpy(candidate, &header, sizeof(header));

        DecodedChunkView decoded[2] = {};
        LoadReport report{};
        const auto scanResult =
            scan(candidate, encodeResult.bytesWritten, decoded, 2, &report);
        assert(scanResult.status == Status::OK);
        assert(scanResult.chunkCount == 1);
        assert(!scanResult.overwriteSafe);
        assert(report.status == LoadStatus::INSPECTION_ISSUES);
        assert(report.hasUnknownUnsupportedData);
        assert(reportHas(report, LoadCode::UNSUPPORTED_CONTAINER_VERSION));
        assert(decoded[0].id == chunkIdValue(ChunkId::EDITING));
    }

    std::cout
        << "[PASS] noncurrent containers are inspection-only\n";
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

void test_noncanonical_byte_regions_are_rejected_before_chunk_publication() {
    ContainerBytes canonical{};
    const uint32_t canonicalSize = encodeCanonicalTwoChunkFixture(canonical);

    {
        auto candidate = canonical;
        uint32_t candidateSize = canonicalSize;
        auto header = readHeader(candidate);
        insertAnonymousByte(candidate, candidateSize, header.directoryOffset);
        ++header.directoryOffset;
        ++header.payloadOffset;
        writeHeader(candidate, header);
        for (uint16_t i = 0U; i < header.chunkCount; ++i) {
            auto entry = readDirectoryEntry(candidate, header, i);
            ++entry.offset;
            writeDirectoryEntry(candidate, header, i, entry);
        }
        assertNonCanonicalLayoutRejected(candidate, candidateSize);
    }

    {
        auto candidate = canonical;
        uint32_t candidateSize = canonicalSize;
        auto header = readHeader(candidate);
        insertAnonymousByte(candidate, candidateSize, header.payloadOffset);
        ++header.payloadOffset;
        for (uint16_t i = 0U; i < header.chunkCount; ++i) {
            auto entry = readDirectoryEntry(candidate, header, i);
            ++entry.offset;
            writeDirectoryEntry(candidate, header, i, entry);
        }
        writeHeader(candidate, header);
        assertNonCanonicalLayoutRejected(candidate, candidateSize);
    }

    {
        auto candidate = canonical;
        uint32_t candidateSize = canonicalSize;
        const auto header = readHeader(candidate);
        const auto first = readDirectoryEntry(candidate, header, 0U);
        auto second = readDirectoryEntry(candidate, header, 1U);
        insertAnonymousByte(candidate, candidateSize, first.offset + first.size);
        ++second.offset;
        writeDirectoryEntry(candidate, header, 1U, second);
        assertNonCanonicalLayoutRejected(candidate, candidateSize);
    }

    {
        auto candidate = canonical;
        const auto header = readHeader(candidate);
        const auto first = readDirectoryEntry(candidate, header, 0U);
        auto second = readDirectoryEntry(candidate, header, 1U);
        second.offset = first.offset + 2U;
        writeDirectoryEntry(candidate, header, 1U, second);
        assertNonCanonicalLayoutRejected(candidate, canonicalSize - second.size);
    }

    {
        auto candidate = canonical;
        candidate[canonicalSize] = 0xA5U;
        assertNonCanonicalLayoutRejected(candidate, canonicalSize + 1U);
    }

    std::cout << "[PASS] noncanonical byte regions reject before chunk publication\n";
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
    test_noncurrent_container_versions_can_be_scanned_for_inspection_only();
    test_encode_reports_required_size_when_buffer_too_small();
    test_noncanonical_byte_regions_are_rejected_before_chunk_publication();

    std::cout << "\n==============================================\n";
    std::cout << "All tests passed\n";
    std::cout << "==============================================\n";
    return 0;
}

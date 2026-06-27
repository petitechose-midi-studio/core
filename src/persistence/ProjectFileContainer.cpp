#include "persistence/ProjectFileContainer.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::persistence::project_file {

namespace {

#pragma pack(push, 1)
struct FileHeader {
    uint32_t magic = PROJECT_FILE_MAGIC;
    uint8_t versionMajor = CONTAINER_VERSION_MAJOR;
    uint8_t versionMinor = CONTAINER_VERSION_MINOR;
    uint16_t headerSize = sizeof(FileHeader);
    uint16_t chunkCount = 0;
    uint16_t directoryEntrySize = 0;
    uint32_t directoryOffset = 0;
    uint32_t payloadOffset = 0;
    uint32_t modifiedCounter = 0;
    uint32_t reserved0 = 0;
};

struct ChunkDirectoryEntry {
    uint32_t id = 0;
    uint8_t versionMajor = 0;
    uint8_t versionMinor = 0;
    uint16_t flags = 0;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t crc32 = 0;
};
#pragma pack(pop)

static_assert(sizeof(FileHeader) == 28, "Unexpected project file header size");
static_assert(sizeof(ChunkDirectoryEntry) == 20, "Unexpected project chunk directory size");

constexpr uint16_t kDirectoryEntrySize = sizeof(ChunkDirectoryEntry);
constexpr uint32_t kHeaderSize = sizeof(FileHeader);

FLASHMEM bool addOverflow(uint32_t lhs, uint32_t rhs, uint32_t& out) {
    if (lhs > UINT32_MAX - rhs) return true;
    out = lhs + rhs;
    return false;
}

FLASHMEM bool rangeInside(uint32_t offset, uint32_t length, uint32_t total) {
    uint32_t end = 0;
    if (addOverflow(offset, length, end)) return false;
    return offset <= total && end <= total;
}

FLASHMEM void report(LoadReport* loadReport,
                     LoadSeverity severity,
                     LoadCode code,
                     uint32_t chunkId = 0,
                     uint8_t sourceMajor = 0,
                     uint8_t sourceMinor = 0) {
    if (loadReport == nullptr) return;
    loadReport->add(severity,
                    code,
                    chunkId,
                    sourceMajor,
                    sourceMinor,
                    CONTAINER_VERSION_MAJOR,
                    CONTAINER_VERSION_MINOR);
}

FLASHMEM bool containsChunk(const DecodedChunkView* chunks, uint16_t count, uint32_t id) {
    if (chunks == nullptr) return false;
    for (uint16_t i = 0; i < count; ++i) {
        if (chunks[i].id == id) return true;
    }
    return false;
}

FLASHMEM Status statusForReport(const LoadReport* loadReport) {
    if (loadReport == nullptr) return Status::OK;
    return loadReport->status == LoadStatus::FAILED ? Status::INVALID_CONTAINER : Status::OK;
}

}  // namespace

FLASHMEM void LoadReport::reset() {
    status = LoadStatus::OK;
    overwriteSafe = true;
    hasUnknownUnsupportedData = false;
    items = {};
    itemCount = 0;
}

FLASHMEM void LoadReport::add(LoadSeverity severity,
                              LoadCode code,
                              uint32_t chunkId,
                              uint8_t sourceMajor,
                              uint8_t sourceMinor,
                              uint8_t targetMajor,
                              uint8_t targetMinor) {
    if (itemCount < MAX_ITEMS) {
        items[itemCount++] = {
            .severity = severity,
            .code = code,
            .chunkId = chunkId,
            .sourceMajor = sourceMajor,
            .sourceMinor = sourceMinor,
            .targetMajor = targetMajor,
            .targetMinor = targetMinor,
        };
    }

    if (code == LoadCode::MIGRATED_CHUNK && status == LoadStatus::OK) {
        status = LoadStatus::MIGRATED;
    } else if (severity == LoadSeverity::WARNING &&
               (status == LoadStatus::OK || status == LoadStatus::MIGRATED)) {
        status = LoadStatus::PARTIAL;
    } else if (severity == LoadSeverity::ERROR) {
        status = LoadStatus::PARTIAL;
    } else if (severity == LoadSeverity::FATAL) {
        status = LoadStatus::FAILED;
    }

    if (severity != LoadSeverity::INFO) {
        overwriteSafe = false;
    }
    if (code == LoadCode::UNKNOWN_CHUNK ||
        code == LoadCode::UNSUPPORTED_CONTAINER_VERSION ||
        code == LoadCode::OUTPUT_CAPACITY_EXCEEDED ||
        code == LoadCode::UNSUPPORTED_CHUNK_VERSION) {
        hasUnknownUnsupportedData = true;
        overwriteSafe = false;
    }
}

FLASHMEM bool isKnownChunkId(uint32_t id) {
    switch (static_cast<ChunkId>(id)) {
        case ChunkId::MANIFEST:
        case ChunkId::PROJECT_META:
        case ChunkId::TRANSPORT:
        case ChunkId::MUSICAL_CONTEXT:
        case ChunkId::ROUTING:
        case ChunkId::EDITING:
        case ChunkId::MACRO_STATE:
        case ChunkId::MACRO_AUTOMATION:
        case ChunkId::SEQUENCER_STATE:
        case ChunkId::HISTORY_JOURNAL:
            return true;
        default:
            return false;
    }
}

FLASHMEM uint32_t crc32(const uint8_t* data, size_t size) {
    uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i) {
        crc ^= static_cast<uint32_t>(data[i]);
        for (uint8_t bit = 0; bit < 8; ++bit) {
            const uint32_t mask = static_cast<uint32_t>(-(static_cast<int32_t>(crc & 1u)));
            crc = (crc >> 1u) ^ (0xEDB88320u & mask);
        }
    }
    return ~crc;
}

FLASHMEM uint32_t encodedSize(const ChunkView* chunks, uint16_t chunkCount) {
    if (chunkCount > MAX_CHUNKS) return 0;
    uint32_t size = kHeaderSize + static_cast<uint32_t>(chunkCount) * kDirectoryEntrySize;
    for (uint16_t i = 0; i < chunkCount; ++i) {
        uint32_t next = 0;
        if (addOverflow(size, chunks[i].size, next)) return 0;
        size = next;
    }
    return size;
}

FLASHMEM EncodeResult encode(const ChunkView* chunks,
                             uint16_t chunkCount,
                             uint32_t modifiedCounter,
                             uint8_t* out,
                             uint32_t outCapacity) {
    if (out == nullptr || (chunkCount > 0 && chunks == nullptr)) {
        return {.status = Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }
    if (chunkCount > MAX_CHUNKS) {
        return {.status = Status::TOO_MANY_CHUNKS, .bytesWritten = 0};
    }

    const uint32_t required = encodedSize(chunks, chunkCount);
    if (required == 0) {
        return {.status = Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }
    if (outCapacity < required) {
        return {.status = Status::BUFFER_TOO_SMALL, .bytesWritten = required};
    }

    for (uint16_t i = 0; i < chunkCount; ++i) {
        if (chunks[i].size > 0 && chunks[i].data == nullptr) {
            return {.status = Status::INVALID_ARGUMENT, .bytesWritten = 0};
        }
    }

    const uint32_t directoryOffset = kHeaderSize;
    const uint32_t payloadOffset =
        directoryOffset + static_cast<uint32_t>(chunkCount) * kDirectoryEntrySize;

    FileHeader header{};
    header.chunkCount = chunkCount;
    header.directoryEntrySize = kDirectoryEntrySize;
    header.directoryOffset = directoryOffset;
    header.payloadOffset = payloadOffset;
    header.modifiedCounter = modifiedCounter;
    std::memcpy(out, &header, sizeof(header));

    uint32_t payloadCursor = payloadOffset;
    for (uint16_t i = 0; i < chunkCount; ++i) {
        ChunkDirectoryEntry entry{};
        entry.id = chunks[i].id;
        entry.versionMajor = chunks[i].versionMajor;
        entry.versionMinor = chunks[i].versionMinor;
        entry.flags = chunks[i].flags;
        entry.offset = payloadCursor;
        entry.size = chunks[i].size;
        entry.crc32 = crc32(chunks[i].data, chunks[i].size);

        std::memcpy(out + directoryOffset + static_cast<uint32_t>(i) * sizeof(entry),
                    &entry,
                    sizeof(entry));

        if (chunks[i].size > 0) {
            std::memcpy(out + payloadCursor, chunks[i].data, chunks[i].size);
        }
        payloadCursor += chunks[i].size;
    }

    return {.status = Status::OK, .bytesWritten = required};
}

FLASHMEM DecodeResult decode(const uint8_t* data,
                             uint32_t size,
                             DecodedChunkView* outChunks,
                             uint16_t outCapacity,
                             LoadReport* loadReport) {
    if (loadReport != nullptr) {
        loadReport->reset();
    }

    if (data == nullptr || outChunks == nullptr) {
        report(loadReport, LoadSeverity::FATAL, LoadCode::INVALID_HEADER);
        return {.status = Status::INVALID_ARGUMENT, .chunkCount = 0, .overwriteSafe = false};
    }
    if (size < sizeof(FileHeader)) {
        report(loadReport, LoadSeverity::FATAL, LoadCode::BUFFER_TOO_SMALL);
        return {.status = Status::INVALID_CONTAINER, .chunkCount = 0, .overwriteSafe = false};
    }

    FileHeader header{};
    std::memcpy(&header, data, sizeof(header));
    if (header.magic != PROJECT_FILE_MAGIC) {
        report(loadReport, LoadSeverity::FATAL, LoadCode::INVALID_MAGIC);
        return {.status = Status::INVALID_CONTAINER, .chunkCount = 0, .overwriteSafe = false};
    }

    if (header.versionMajor > CONTAINER_VERSION_MAJOR) {
        report(loadReport,
               LoadSeverity::WARNING,
               LoadCode::UNSUPPORTED_CONTAINER_VERSION,
               0,
               header.versionMajor,
               header.versionMinor);
    }

    if (header.headerSize != sizeof(FileHeader) ||
        header.directoryEntrySize != sizeof(ChunkDirectoryEntry) ||
        header.chunkCount > MAX_CHUNKS ||
        header.directoryOffset < sizeof(FileHeader)) {
        report(loadReport, LoadSeverity::FATAL, LoadCode::INVALID_HEADER);
        return {.status = Status::INVALID_CONTAINER, .chunkCount = 0, .overwriteSafe = false};
    }

    const uint32_t directoryBytes =
        static_cast<uint32_t>(header.chunkCount) * sizeof(ChunkDirectoryEntry);
    if (!rangeInside(header.directoryOffset, directoryBytes, size) ||
        header.payloadOffset < header.directoryOffset + directoryBytes ||
        header.payloadOffset > size) {
        report(loadReport, LoadSeverity::FATAL, LoadCode::CHUNK_DIRECTORY_INVALID);
        return {.status = Status::INVALID_CONTAINER, .chunkCount = 0, .overwriteSafe = false};
    }

    uint16_t decodedCount = 0;
    for (uint16_t i = 0; i < header.chunkCount; ++i) {
        ChunkDirectoryEntry entry{};
        const uint32_t entryOffset =
            header.directoryOffset + static_cast<uint32_t>(i) * sizeof(entry);
        std::memcpy(&entry, data + entryOffset, sizeof(entry));

        if (!rangeInside(entry.offset, entry.size, size) || entry.offset < header.payloadOffset) {
            report(loadReport, LoadSeverity::ERROR, LoadCode::CHUNK_OUT_OF_BOUNDS, entry.id);
            continue;
        }

        const uint8_t* payload = data + entry.offset;
        const uint32_t actualCrc = crc32(payload, entry.size);
        if (actualCrc != entry.crc32) {
            report(loadReport, LoadSeverity::ERROR, LoadCode::CHUNK_CRC_MISMATCH, entry.id);
            continue;
        }

        const bool known = isKnownChunkId(entry.id);
        if (!known) {
            report(loadReport,
                   LoadSeverity::WARNING,
                   LoadCode::UNKNOWN_CHUNK,
                   entry.id,
                   entry.versionMajor,
                   entry.versionMinor);
        }
        if (containsChunk(outChunks, decodedCount, entry.id)) {
            report(loadReport, LoadSeverity::ERROR, LoadCode::DUPLICATE_CHUNK, entry.id);
            continue;
        }
        if (decodedCount >= outCapacity) {
            report(loadReport, LoadSeverity::WARNING, LoadCode::OUTPUT_CAPACITY_EXCEEDED);
            continue;
        }

        outChunks[decodedCount++] = {
            .id = entry.id,
            .versionMajor = entry.versionMajor,
            .versionMinor = entry.versionMinor,
            .flags = entry.flags,
            .data = payload,
            .size = entry.size,
            .known = known,
        };
    }

    const Status status = statusForReport(loadReport);
    const bool overwriteSafe = (loadReport == nullptr) ? true : loadReport->overwriteSafe;
    return {.status = status, .chunkCount = decodedCount, .overwriteSafe = overwriteSafe};
}

}  // namespace core::persistence::project_file

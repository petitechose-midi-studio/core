#include "persistence/ProjectFileContainer.hpp"

#include <algorithm>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/PersistenceBinaryCodec.hpp"
#include "persistence/PersistenceChecksum.hpp"

namespace core::persistence::project_file {

namespace {

namespace binary = core::persistence::binary_codec;

constexpr uint16_t kHeaderSize = 28;
constexpr uint16_t kDirectoryEntrySize = 20;

struct FileHeader {
    uint32_t magic = PROJECT_FILE_MAGIC;
    uint8_t versionMajor = CONTAINER_VERSION_MAJOR;
    uint8_t versionMinor = CONTAINER_VERSION_MINOR;
    uint16_t headerSize = kHeaderSize;
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

static_assert(kHeaderSize == 28, "Unexpected project file header size");
static_assert(kDirectoryEntrySize == 20, "Unexpected project chunk directory size");

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

FLASHMEM bool writeFileHeader(uint8_t* out, uint32_t outCapacity, const FileHeader& header) {
    binary::Writer writer(out, outCapacity);
    return writer.writeU32(header.magic) &&
           writer.writeU8(header.versionMajor) &&
           writer.writeU8(header.versionMinor) &&
           writer.writeU16(header.headerSize) &&
           writer.writeU16(header.chunkCount) &&
           writer.writeU16(header.directoryEntrySize) &&
           writer.writeU32(header.directoryOffset) &&
           writer.writeU32(header.payloadOffset) &&
           writer.writeU32(header.modifiedCounter) &&
           writer.writeU32(0) &&
           writer.ok() &&
           writer.offset() == kHeaderSize;
}

FLASHMEM bool readFileHeader(const uint8_t* data, uint32_t size, FileHeader& out) {
    if (size < kHeaderSize) return false;
    binary::Reader reader(data, size);
    uint32_t reserved = 0;
    return reader.readU32(out.magic) &&
           reader.readU8(out.versionMajor) &&
           reader.readU8(out.versionMinor) &&
           reader.readU16(out.headerSize) &&
           reader.readU16(out.chunkCount) &&
           reader.readU16(out.directoryEntrySize) &&
           reader.readU32(out.directoryOffset) &&
           reader.readU32(out.payloadOffset) &&
           reader.readU32(out.modifiedCounter) &&
           reader.readU32(reserved) &&
           reader.ok() &&
           reader.offset() == kHeaderSize;
}

FLASHMEM bool writeChunkDirectoryEntry(uint8_t* out,
                                       uint32_t outCapacity,
                                       const ChunkDirectoryEntry& entry) {
    binary::Writer writer(out, outCapacity);
    return writer.writeU32(entry.id) &&
           writer.writeU8(entry.versionMajor) &&
           writer.writeU8(entry.versionMinor) &&
           writer.writeU16(entry.flags) &&
           writer.writeU32(entry.offset) &&
           writer.writeU32(entry.size) &&
           writer.writeU32(entry.crc32) &&
           writer.ok() &&
           writer.offset() == kDirectoryEntrySize;
}

FLASHMEM bool readChunkDirectoryEntry(const uint8_t* data,
                                      uint32_t size,
                                      ChunkDirectoryEntry& out) {
    if (size < kDirectoryEntrySize) return false;
    binary::Reader reader(data, size);
    return reader.readU32(out.id) &&
           reader.readU8(out.versionMajor) &&
           reader.readU8(out.versionMinor) &&
           reader.readU16(out.flags) &&
           reader.readU32(out.offset) &&
           reader.readU32(out.size) &&
           reader.readU32(out.crc32) &&
           reader.ok() &&
           reader.offset() == kDirectoryEntrySize;
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

    if (severity == LoadSeverity::WARNING && status == LoadStatus::OK) {
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
        case ChunkId::EDITING:
        case ChunkId::TRACK_STATE:
        case ChunkId::MACRO_STATE:
        case ChunkId::MACRO_AUTOMATION:
        case ChunkId::MODULATION_GRAPH:
        case ChunkId::SEQUENCER_STATE:
        case ChunkId::HISTORY_JOURNAL:
            return true;
        default:
            return false;
    }
}

FLASHMEM uint32_t crc32(const uint8_t* data, size_t size) {
    return checksum::crc32(data, size);
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
    if (!writeFileHeader(out, outCapacity, header)) {
        return {.status = Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }

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

        const uint32_t entryOffset =
            directoryOffset + static_cast<uint32_t>(i) * kDirectoryEntrySize;
        if (!writeChunkDirectoryEntry(out + entryOffset, outCapacity - entryOffset, entry)) {
            return {.status = Status::INVALID_ARGUMENT, .bytesWritten = 0};
        }

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
    LoadReport localReport{};
    LoadReport* effectiveReport = loadReport != nullptr ? loadReport : &localReport;
    effectiveReport->reset();

    if (data == nullptr || outChunks == nullptr) {
        report(effectiveReport, LoadSeverity::FATAL, LoadCode::INVALID_HEADER);
        return {.status = Status::INVALID_ARGUMENT, .chunkCount = 0, .overwriteSafe = false};
    }
    if (size < kHeaderSize) {
        report(effectiveReport, LoadSeverity::FATAL, LoadCode::BUFFER_TOO_SMALL);
        return {.status = Status::INVALID_CONTAINER, .chunkCount = 0, .overwriteSafe = false};
    }

    FileHeader header{};
    if (!readFileHeader(data, size, header)) {
        report(effectiveReport, LoadSeverity::FATAL, LoadCode::INVALID_HEADER);
        return {.status = Status::INVALID_CONTAINER, .chunkCount = 0, .overwriteSafe = false};
    }
    if (header.magic != PROJECT_FILE_MAGIC) {
        report(effectiveReport, LoadSeverity::FATAL, LoadCode::INVALID_MAGIC);
        return {.status = Status::INVALID_CONTAINER, .chunkCount = 0, .overwriteSafe = false};
    }

    if (header.versionMajor > CONTAINER_VERSION_MAJOR) {
        report(effectiveReport,
               LoadSeverity::WARNING,
               LoadCode::UNSUPPORTED_CONTAINER_VERSION,
               0,
               header.versionMajor,
               header.versionMinor);
    }

    if (header.headerSize != kHeaderSize ||
        header.directoryEntrySize != kDirectoryEntrySize ||
        header.chunkCount > MAX_CHUNKS ||
        header.directoryOffset < kHeaderSize) {
        report(effectiveReport, LoadSeverity::FATAL, LoadCode::INVALID_HEADER);
        return {.status = Status::INVALID_CONTAINER, .chunkCount = 0, .overwriteSafe = false};
    }

    const uint32_t directoryBytes =
        static_cast<uint32_t>(header.chunkCount) * kDirectoryEntrySize;
    if (!rangeInside(header.directoryOffset, directoryBytes, size) ||
        header.payloadOffset < header.directoryOffset + directoryBytes ||
        header.payloadOffset > size) {
        report(effectiveReport, LoadSeverity::FATAL, LoadCode::CHUNK_DIRECTORY_INVALID);
        return {.status = Status::INVALID_CONTAINER, .chunkCount = 0, .overwriteSafe = false};
    }

    uint16_t decodedCount = 0;
    for (uint16_t i = 0; i < header.chunkCount; ++i) {
        ChunkDirectoryEntry entry{};
        const uint32_t entryOffset =
            header.directoryOffset + static_cast<uint32_t>(i) * kDirectoryEntrySize;
        if (!readChunkDirectoryEntry(data + entryOffset, size - entryOffset, entry)) {
            report(effectiveReport, LoadSeverity::ERROR, LoadCode::CHUNK_DIRECTORY_INVALID);
            continue;
        }

        if (!rangeInside(entry.offset, entry.size, size) || entry.offset < header.payloadOffset) {
            report(effectiveReport, LoadSeverity::ERROR, LoadCode::CHUNK_OUT_OF_BOUNDS, entry.id);
            continue;
        }

        const uint8_t* payload = data + entry.offset;
        const uint32_t actualCrc = crc32(payload, entry.size);
        if (actualCrc != entry.crc32) {
            report(effectiveReport, LoadSeverity::ERROR, LoadCode::CHUNK_CRC_MISMATCH, entry.id);
            continue;
        }

        const bool known = isKnownChunkId(entry.id);
        if (!known) {
            report(effectiveReport,
                   LoadSeverity::WARNING,
                   LoadCode::UNKNOWN_CHUNK,
                   entry.id,
                   entry.versionMajor,
                   entry.versionMinor);
        }
        if (containsChunk(outChunks, decodedCount, entry.id)) {
            report(effectiveReport, LoadSeverity::ERROR, LoadCode::DUPLICATE_CHUNK, entry.id);
            continue;
        }
        if (decodedCount >= outCapacity) {
            report(effectiveReport, LoadSeverity::WARNING, LoadCode::OUTPUT_CAPACITY_EXCEEDED);
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

    const Status status = statusForReport(effectiveReport);
    const bool overwriteSafe = effectiveReport->overwriteSafe;
    return {.status = status, .chunkCount = decodedCount, .overwriteSafe = overwriteSafe};
}

}  // namespace core::persistence::project_file

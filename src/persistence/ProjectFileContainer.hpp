#pragma once

#include <cstddef>
#include <cstdint>

#include "persistence/ProjectLoadReport.hpp"

namespace core::persistence::project_file {

inline constexpr uint32_t PROJECT_FILE_MAGIC = 0x4D53504Au;  // "MSPJ"
inline constexpr uint8_t CONTAINER_VERSION_MAJOR = 1;
inline constexpr uint8_t CONTAINER_VERSION_MINOR = 0;
inline constexpr uint16_t MAX_CHUNKS = 16;

enum class ChunkId : uint32_t {
    MANIFEST = 0x4D414E46u,         // "MANF"
    PROJECT_META = 0x4D455441u,     // "META"
    TRANSPORT = 0x54525054u,        // "TRPT"
    MUSICAL_CONTEXT = 0x4D555343u,  // "MUSC"
    EDITING = 0x45444954u,          // "EDIT"
    TRACK_STATE = 0x54524B53u,      // "TRKS"
    MACRO_STATE = 0x4D414352u,      // "MACR"
    MACRO_AUTOMATION = 0x4D415554u, // "MAUT"
    MODULATION_GRAPH = 0x4D4F4447u, // "MODG"
    SEQUENCER_STATE = 0x53455152u,  // "SEQR"
    HISTORY_JOURNAL = 0x48495354u,  // "HIST"
};

enum class Status : uint8_t {
    OK = 0,
    INVALID_ARGUMENT,
    BUFFER_TOO_SMALL,
    TOO_MANY_CHUNKS,
    INVALID_CONTAINER,
    OUTPUT_CAPACITY_EXCEEDED,
    SCRATCH_ALLOCATION_FAILED,
};

struct ChunkView {
    uint32_t id = 0;
    uint8_t versionMajor = 1;
    uint8_t versionMinor = 0;
    uint16_t flags = 0;
    const uint8_t* data = nullptr;
    uint32_t size = 0;
};

struct DecodedChunkView {
    uint32_t id = 0;
    uint8_t versionMajor = 0;
    uint8_t versionMinor = 0;
    uint16_t flags = 0;
    const uint8_t* data = nullptr;
    uint32_t size = 0;
    bool known = false;
};

struct EncodeResult {
    Status status = Status::OK;
    uint32_t bytesWritten = 0;
};

struct ScanResult {
    Status status = Status::OK;
    uint16_t chunkCount = 0;
    bool overwriteSafe = true;
};

constexpr uint32_t chunkIdValue(ChunkId id) {
    return static_cast<uint32_t>(id);
}

bool isKnownChunkId(uint32_t id);
uint32_t crc32(const uint8_t* data, size_t size);
uint32_t encodedSize(const ChunkView* chunks, uint16_t chunkCount);

EncodeResult encode(const ChunkView* chunks,
                    uint16_t chunkCount,
                    uint32_t modifiedCounter,
                    uint8_t* out,
                    uint32_t outCapacity);

/**
 * Performs a bounded structural scan only.
 *
 * The returned chunk views are inspection data. Runtime state publication
 * must go through the strict, atomic ProjectSnapshotPersistenceCodec.
 */
ScanResult scan(const uint8_t* data,
                uint32_t size,
                DecodedChunkView* outChunks,
                uint16_t outCapacity,
                LoadReport* report = nullptr);

}  // namespace core::persistence::project_file

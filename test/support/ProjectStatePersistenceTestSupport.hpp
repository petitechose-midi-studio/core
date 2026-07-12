#pragma once

#include <array>
#include <cstdint>

#include "persistence/ProjectStatePersistenceCodec.hpp"

namespace core::test::project_state_persistence {

namespace codec = core::persistence::project_state_codec;
namespace project_file = core::persistence::project_file;

struct DecodeResult {
    bool ok = false;
    project_file::Status containerStatus = project_file::Status::OK;
    project_file::LoadStatus loadStatus = project_file::LoadStatus::OK;
    bool overwriteSafe = true;
};

inline project_file::EncodeResult encode(
    const core::state::project::ProjectState& state,
    uint8_t* out,
    uint32_t outCapacity
) {
    codec::ProjectMetaPayload meta{};
    codec::ProjectTransportPayload transport{};
    codec::ProjectMusicalContextPayload musical{};
    codec::ProjectRoutingPayload routing{};
    codec::ProjectEditingPayload editing{};
    codec::fillMetaPayload(state.metadata, meta);
    codec::fillTransportPayload(state.transport, transport);
    codec::fillMusicalContextPayload(state.musical, musical);
    codec::fillRoutingPayload(state.routing, routing);
    codec::fillEditingPayload(state.editing, editing);

    std::array<uint8_t, codec::PROJECT_META_PAYLOAD_SIZE> metaBytes{};
    std::array<uint8_t, codec::PROJECT_TRANSPORT_PAYLOAD_SIZE> transportBytes{};
    std::array<uint8_t, codec::PROJECT_MUSICAL_CONTEXT_PAYLOAD_SIZE> musicalBytes{};
    std::array<uint8_t, codec::PROJECT_ROUTING_PAYLOAD_SIZE> routingBytes{};
    std::array<uint8_t, codec::PROJECT_EDITING_PAYLOAD_SIZE> editingBytes{};
    if (!codec::encodeMetaPayload(meta, metaBytes.data(), metaBytes.size()) ||
        !codec::encodeTransportPayload(transport, transportBytes.data(), transportBytes.size()) ||
        !codec::encodeMusicalContextPayload(musical, musicalBytes.data(), musicalBytes.size()) ||
        !codec::encodeRoutingPayload(routing, routingBytes.data(), routingBytes.size()) ||
        !codec::encodeEditingPayload(editing, editingBytes.data(), editingBytes.size())) {
        return {.status = project_file::Status::INVALID_ARGUMENT, .bytesWritten = 0};
    }

    const std::array<project_file::ChunkView, 5> chunks{{
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::PROJECT_META),
            .versionMajor = codec::PROJECT_STATE_CHUNK_VERSION_MAJOR,
            .versionMinor = codec::PROJECT_STATE_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = metaBytes.data(),
            .size = metaBytes.size(),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::TRANSPORT),
            .versionMajor = codec::PROJECT_STATE_CHUNK_VERSION_MAJOR,
            .versionMinor = codec::PROJECT_STATE_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = transportBytes.data(),
            .size = transportBytes.size(),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::MUSICAL_CONTEXT),
            .versionMajor = codec::PROJECT_STATE_CHUNK_VERSION_MAJOR,
            .versionMinor = codec::PROJECT_STATE_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = musicalBytes.data(),
            .size = musicalBytes.size(),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::ROUTING),
            .versionMajor = codec::PROJECT_STATE_CHUNK_VERSION_MAJOR,
            .versionMinor = codec::PROJECT_STATE_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = routingBytes.data(),
            .size = routingBytes.size(),
        },
        {
            .id = project_file::chunkIdValue(project_file::ChunkId::EDITING),
            .versionMajor = codec::PROJECT_STATE_CHUNK_VERSION_MAJOR,
            .versionMinor = codec::PROJECT_STATE_CHUNK_VERSION_MINOR,
            .flags = 0,
            .data = editingBytes.data(),
            .size = editingBytes.size(),
        },
    }};

    return project_file::encode(
        chunks.data(),
        static_cast<uint16_t>(chunks.size()),
        state.metadata.modifiedCounter,
        out,
        outCapacity
    );
}

inline DecodeResult decode(
    const uint8_t* data,
    uint32_t size,
    core::state::project::ProjectState& out,
    project_file::LoadReport* report = nullptr
) {
    project_file::LoadReport localReport{};
    auto* effectiveReport = report != nullptr ? report : &localReport;
    std::array<project_file::DecodedChunkView, project_file::MAX_CHUNKS> chunks{};
    const auto decoded = project_file::decode(
        data,
        size,
        chunks.data(),
        static_cast<uint16_t>(chunks.size()),
        effectiveReport
    );
    if (decoded.status != project_file::Status::OK) {
        return {
            .ok = false,
            .containerStatus = decoded.status,
            .loadStatus = effectiveReport->status,
            .overwriteSafe = false,
        };
    }

    core::state::project::ProjectState next;
    codec::applyProjectStateChunks(
        chunks.data(),
        decoded.chunkCount,
        next,
        effectiveReport
    );
    out = next;
    return {
        .ok = effectiveReport->status != project_file::LoadStatus::FAILED,
        .containerStatus = decoded.status,
        .loadStatus = effectiveReport->status,
        .overwriteSafe = effectiveReport->overwriteSafe,
    };
}

}  // namespace core::test::project_state_persistence

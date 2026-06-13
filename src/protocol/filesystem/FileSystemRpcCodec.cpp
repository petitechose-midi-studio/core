#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <config/PlatformCompat.hpp>

namespace core::protocol::filesystem {

using oc::type::ErrorCode;
using oc::type::Result;
using internal::ByteReader;
using internal::ByteWriter;
using internal::writeFrameHeader;

FLASHMEM bool FileSystemRpcCodec::isFileSystemMessageId(uint8_t messageId) {
    return messageId >= FILESYSTEM_RPC_ID_MIN && messageId <= FILESYSTEM_RPC_ID_MAX;
}

FLASHMEM bool FileSystemRpcCodec::isFileSystemRequestId(uint8_t messageId) {
    switch (static_cast<FileSystemRpcMessageId>(messageId)) {
        case FileSystemRpcMessageId::STAT_REQUEST:
        case FileSystemRpcMessageId::CAPABILITIES_REQUEST:
        case FileSystemRpcMessageId::LIST_REQUEST:
        case FileSystemRpcMessageId::READ_REQUEST:
        case FileSystemRpcMessageId::WRITE_BEGIN_REQUEST:
        case FileSystemRpcMessageId::WRITE_CHUNK_REQUEST:
        case FileSystemRpcMessageId::WRITE_COMMIT_REQUEST:
        case FileSystemRpcMessageId::WRITE_ABORT_REQUEST:
        case FileSystemRpcMessageId::MKDIR_REQUEST:
        case FileSystemRpcMessageId::DELETE_REQUEST:
        case FileSystemRpcMessageId::RENAME_REQUEST:
            return true;
        default:
            return false;
    }
}

FLASHMEM const char* FileSystemRpcCodec::messageName(FileSystemRpcMessageId messageId) {
    switch (messageId) {
        case FileSystemRpcMessageId::STAT_REQUEST:
            return "FsStatRequest";
        case FileSystemRpcMessageId::STAT_RESPONSE:
            return "FsStatResponse";
        case FileSystemRpcMessageId::CAPABILITIES_REQUEST:
            return "FsCapabilitiesRequest";
        case FileSystemRpcMessageId::CAPABILITIES_RESPONSE:
            return "FsCapabilitiesResponse";
        case FileSystemRpcMessageId::LIST_REQUEST:
            return "FsListRequest";
        case FileSystemRpcMessageId::LIST_RESPONSE:
            return "FsListResponse";
        case FileSystemRpcMessageId::READ_REQUEST:
            return "FsReadRequest";
        case FileSystemRpcMessageId::READ_RESPONSE:
            return "FsReadResponse";
        case FileSystemRpcMessageId::WRITE_BEGIN_REQUEST:
            return "FsWriteBeginRequest";
        case FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE:
            return "FsWriteBeginResponse";
        case FileSystemRpcMessageId::WRITE_CHUNK_REQUEST:
            return "FsWriteChunkRequest";
        case FileSystemRpcMessageId::WRITE_CHUNK_RESPONSE:
            return "FsWriteChunkResponse";
        case FileSystemRpcMessageId::WRITE_COMMIT_REQUEST:
            return "FsWriteCommitRequest";
        case FileSystemRpcMessageId::WRITE_COMMIT_RESPONSE:
            return "FsWriteCommitResponse";
        case FileSystemRpcMessageId::WRITE_ABORT_REQUEST:
            return "FsWriteAbortRequest";
        case FileSystemRpcMessageId::WRITE_ABORT_RESPONSE:
            return "FsWriteAbortResponse";
        case FileSystemRpcMessageId::MKDIR_REQUEST:
            return "FsMkdirRequest";
        case FileSystemRpcMessageId::MKDIR_RESPONSE:
            return "FsMkdirResponse";
        case FileSystemRpcMessageId::DELETE_REQUEST:
            return "FsDeleteRequest";
        case FileSystemRpcMessageId::DELETE_RESPONSE:
            return "FsDeleteResponse";
        case FileSystemRpcMessageId::RENAME_REQUEST:
            return "FsRenameRequest";
        case FileSystemRpcMessageId::RENAME_RESPONSE:
            return "FsRenameResponse";
        case FileSystemRpcMessageId::ERROR_RESPONSE:
        default:
            return "FsErrorResponse";
    }
}

FLASHMEM Result<FileSystemRpcFrame> FileSystemRpcCodec::decodeFrame(
    const uint8_t* data,
    size_t size
) {
    if (!data || size < 5) {
        return Result<FileSystemRpcFrame>::err({ErrorCode::INVALID_ARGUMENT, "empty rpc frame"});
    }

    ByteReader reader(data, size);
    uint8_t rawMessageId = 0;
    if (!reader.readU8(rawMessageId) || !isFileSystemMessageId(rawMessageId)) {
        return Result<FileSystemRpcFrame>::err({ErrorCode::INVALID_ARGUMENT, "unknown rpc id"});
    }

    uint8_t nameLength = 0;
    if (!reader.readU8(nameLength)) {
        return Result<FileSystemRpcFrame>::err({ErrorCode::INVALID_ARGUMENT, "missing rpc name"});
    }
    const uint8_t* nameBytes = nullptr;
    if (!reader.readBytes(nameBytes, nameLength)) {
        return Result<FileSystemRpcFrame>::err({ErrorCode::INVALID_ARGUMENT, "truncated rpc name"});
    }

    uint8_t schema = 0;
    uint16_t requestId = 0;
    if (!reader.readU8(schema) || !reader.readU16(requestId)) {
        return Result<FileSystemRpcFrame>::err({ErrorCode::INVALID_ARGUMENT, "truncated rpc header"});
    }

    return Result<FileSystemRpcFrame>::ok(FileSystemRpcFrame{
        static_cast<FileSystemRpcMessageId>(rawMessageId),
        schema,
        requestId,
        reader.current(),
        reader.remaining(),
    });
}

FLASHMEM size_t FileSystemRpcCodec::encodeStatRequest(
    uint16_t requestId,
    const char* path,
    uint8_t* out,
    size_t outSize
) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::STAT_REQUEST, requestId) ||
        !writer.writeString(path, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t FileSystemRpcCodec::encodeCapabilitiesRequest(
    uint16_t requestId,
    uint8_t* out,
    size_t outSize
) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::CAPABILITIES_REQUEST, requestId)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t FileSystemRpcCodec::encodeListRequest(
    uint16_t requestId,
    const char* path,
    uint16_t startIndex,
    uint8_t maxEntries,
    uint8_t* out,
    size_t outSize
) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::LIST_REQUEST, requestId) ||
        !writer.writeU16(startIndex) ||
        !writer.writeU8(maxEntries) ||
        !writer.writeString(path, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t FileSystemRpcCodec::encodeReadRequest(
    uint16_t requestId,
    const char* path,
    uint32_t offset,
    uint16_t size,
    uint8_t* out,
    size_t outSize
) {
    if (size > FILESYSTEM_RPC_MAX_CHUNK_SIZE) return 0;
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::READ_REQUEST, requestId) ||
        !writer.writeU32(offset) ||
        !writer.writeU16(size) ||
        !writer.writeString(path, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t FileSystemRpcCodec::encodeWriteBeginRequest(
    uint16_t requestId,
    uint16_t sessionId,
    const char* path,
    uint32_t expectedSize,
    uint8_t* out,
    size_t outSize
) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::WRITE_BEGIN_REQUEST, requestId) ||
        !writer.writeU16(sessionId) ||
        !writer.writeU32(expectedSize) ||
        !writer.writeString(path, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t FileSystemRpcCodec::encodeWriteChunkRequest(
    uint16_t requestId,
    uint16_t sessionId,
    uint32_t offset,
    const uint8_t* data,
    uint16_t size,
    uint8_t* out,
    size_t outSize
) {
    if (size > FILESYSTEM_RPC_MAX_CHUNK_SIZE || (!data && size > 0)) return 0;
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::WRITE_CHUNK_REQUEST, requestId) ||
        !writer.writeU16(sessionId) ||
        !writer.writeU32(offset) ||
        !writer.writeU16(size) ||
        !writer.writeBytes(data, size)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t FileSystemRpcCodec::encodeWriteCommitRequest(
    uint16_t requestId,
    uint16_t sessionId,
    uint8_t* out,
    size_t outSize
) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::WRITE_COMMIT_REQUEST, requestId) ||
        !writer.writeU16(sessionId)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t FileSystemRpcCodec::encodeWriteAbortRequest(
    uint16_t requestId,
    uint16_t sessionId,
    uint8_t* out,
    size_t outSize
) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::WRITE_ABORT_REQUEST, requestId) ||
        !writer.writeU16(sessionId)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t FileSystemRpcCodec::encodeMkdirRequest(
    uint16_t requestId,
    const char* path,
    uint8_t* out,
    size_t outSize
) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::MKDIR_REQUEST, requestId) ||
        !writer.writeString(path, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t FileSystemRpcCodec::encodeDeleteRequest(
    uint16_t requestId,
    const char* path,
    bool recursive,
    uint8_t* out,
    size_t outSize
) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::DELETE_REQUEST, requestId) ||
        !writer.writeBool(recursive) ||
        !writer.writeString(path, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t FileSystemRpcCodec::encodeRenameRequest(
    uint16_t requestId,
    const char* fromPath,
    const char* toPath,
    uint8_t* out,
    size_t outSize
) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::RENAME_REQUEST, requestId) ||
        !writer.writeString(fromPath, oc::interface::FILESYSTEM_MAX_PATH_LENGTH) ||
        !writer.writeString(toPath, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM Result<FileSystemRpcStatResponse> FileSystemRpcCodec::decodeStatResponse(
    const uint8_t* data,
    size_t size
) {
    auto frame = decodeFrame(data, size);
    if (!frame || frame.value().messageId != FileSystemRpcMessageId::STAT_RESPONSE) {
        return Result<FileSystemRpcStatResponse>::err({ErrorCode::INVALID_ARGUMENT, "not stat response"});
    }

    ByteReader reader(frame.value().payload, frame.value().payloadSize);
    uint8_t rawStatus = 0;
    uint8_t rawType = 0;
    uint32_t sizeBytes = 0;
    if (!reader.readU8(rawStatus)) {
        return Result<FileSystemRpcStatResponse>::err({ErrorCode::INVALID_ARGUMENT, "bad stat response"});
    }
    if (rawStatus == static_cast<uint8_t>(FileSystemRpcStatus::OK)) {
        if (!reader.readU8(rawType) || !reader.readU32(sizeBytes)) {
            return Result<FileSystemRpcStatResponse>::err({ErrorCode::INVALID_ARGUMENT, "bad stat payload"});
        }
    }
    return Result<FileSystemRpcStatResponse>::ok(FileSystemRpcStatResponse{
        frame.value().requestId,
        static_cast<FileSystemRpcStatus>(rawStatus),
        static_cast<FileSystemRpcFileType>(rawType),
        sizeBytes,
    });
}

FLASHMEM Result<FileSystemRpcListResponse> FileSystemRpcCodec::decodeListResponse(
    const uint8_t* data,
    size_t size
) {
    auto frame = decodeFrame(data, size);
    if (!frame || frame.value().messageId != FileSystemRpcMessageId::LIST_RESPONSE) {
        return Result<FileSystemRpcListResponse>::err({ErrorCode::INVALID_ARGUMENT, "not list response"});
    }

    ByteReader reader(frame.value().payload, frame.value().payloadSize);
    uint8_t rawStatus = 0;
    FileSystemRpcListResponse response{};
    response.requestId = frame.value().requestId;
    if (!reader.readU8(rawStatus)) {
        return Result<FileSystemRpcListResponse>::err({ErrorCode::INVALID_ARGUMENT, "bad list response"});
    }
    response.status = static_cast<FileSystemRpcStatus>(rawStatus);
    if (response.status != FileSystemRpcStatus::OK) {
        return Result<FileSystemRpcListResponse>::ok(response);
    }

    uint8_t hasMore = 0;
    if (!reader.readU16(response.startIndex) ||
        !reader.readU8(response.entryCount) ||
        !reader.readU8(hasMore) ||
        response.entryCount > FILESYSTEM_RPC_MAX_LIST_ENTRIES) {
        return Result<FileSystemRpcListResponse>::err({ErrorCode::INVALID_ARGUMENT, "bad list payload"});
    }
    response.hasMore = hasMore != 0;

    for (uint8_t i = 0; i < response.entryCount; ++i) {
        uint8_t rawType = 0;
        bool truncated = false;
        if (!reader.readString(
                response.entries[i].name,
                sizeof(response.entries[i].name),
                oc::interface::FILESYSTEM_MAX_NAME_LENGTH
            ) ||
            !reader.readU8(rawType) ||
            !reader.readU32(response.entries[i].sizeBytes) ||
            !reader.readBool(truncated)) {
            return Result<FileSystemRpcListResponse>::err({ErrorCode::INVALID_ARGUMENT, "bad list entry"});
        }
        response.entries[i].type = static_cast<FileSystemRpcFileType>(rawType);
        response.entries[i].nameTruncated = truncated;
    }

    return Result<FileSystemRpcListResponse>::ok(response);
}

FLASHMEM Result<FileSystemRpcReadResponse> FileSystemRpcCodec::decodeReadResponse(
    const uint8_t* data,
    size_t size
) {
    auto frame = decodeFrame(data, size);
    if (!frame || frame.value().messageId != FileSystemRpcMessageId::READ_RESPONSE) {
        return Result<FileSystemRpcReadResponse>::err({ErrorCode::INVALID_ARGUMENT, "not read response"});
    }

    ByteReader reader(frame.value().payload, frame.value().payloadSize);
    uint8_t rawStatus = 0;
    FileSystemRpcReadResponse response{};
    response.requestId = frame.value().requestId;
    if (!reader.readU8(rawStatus)) {
        return Result<FileSystemRpcReadResponse>::err({ErrorCode::INVALID_ARGUMENT, "bad read response"});
    }
    response.status = static_cast<FileSystemRpcStatus>(rawStatus);
    if (response.status != FileSystemRpcStatus::OK) {
        return Result<FileSystemRpcReadResponse>::ok(response);
    }

    if (!reader.readU32(response.offset) || !reader.readU16(response.bytesRead)) {
        return Result<FileSystemRpcReadResponse>::err({ErrorCode::INVALID_ARGUMENT, "bad read payload"});
    }
    const uint8_t* bytes = nullptr;
    if (!reader.readBytes(bytes, response.bytesRead)) {
        return Result<FileSystemRpcReadResponse>::err({ErrorCode::INVALID_ARGUMENT, "truncated read payload"});
    }
    response.data = bytes;
    return Result<FileSystemRpcReadResponse>::ok(response);
}

FLASHMEM Result<FileSystemRpcWriteResponse> FileSystemRpcCodec::decodeWriteResponse(
    const uint8_t* data,
    size_t size
) {
    auto frame = decodeFrame(data, size);
    if (!frame) {
        return Result<FileSystemRpcWriteResponse>::err(frame.error());
    }
    switch (frame.value().messageId) {
        case FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE:
        case FileSystemRpcMessageId::WRITE_CHUNK_RESPONSE:
        case FileSystemRpcMessageId::WRITE_COMMIT_RESPONSE:
        case FileSystemRpcMessageId::WRITE_ABORT_RESPONSE:
            break;
        default:
            return Result<FileSystemRpcWriteResponse>::err({ErrorCode::INVALID_ARGUMENT, "not write response"});
    }

    ByteReader reader(frame.value().payload, frame.value().payloadSize);
    uint8_t rawStatus = 0;
    FileSystemRpcWriteResponse response{};
    response.requestId = frame.value().requestId;
    if (!reader.readU8(rawStatus) ||
        !reader.readU16(response.sessionId) ||
        !reader.readU16(response.bytesWritten)) {
        return Result<FileSystemRpcWriteResponse>::err({ErrorCode::INVALID_ARGUMENT, "bad write response"});
    }
    response.status = static_cast<FileSystemRpcStatus>(rawStatus);
    return Result<FileSystemRpcWriteResponse>::ok(response);
}

FLASHMEM Result<FileSystemRpcStatusResponse> FileSystemRpcCodec::decodeStatusResponse(
    const uint8_t* data,
    size_t size
) {
    auto frame = decodeFrame(data, size);
    if (!frame) {
        return Result<FileSystemRpcStatusResponse>::err(frame.error());
    }
    switch (frame.value().messageId) {
        case FileSystemRpcMessageId::MKDIR_RESPONSE:
        case FileSystemRpcMessageId::DELETE_RESPONSE:
        case FileSystemRpcMessageId::RENAME_RESPONSE:
        case FileSystemRpcMessageId::ERROR_RESPONSE:
            break;
        default:
            return Result<FileSystemRpcStatusResponse>::err({ErrorCode::INVALID_ARGUMENT, "not status response"});
    }

    ByteReader reader(frame.value().payload, frame.value().payloadSize);
    uint8_t rawStatus = 0;
    if (!reader.readU8(rawStatus) || reader.remaining() != 0) {
        return Result<FileSystemRpcStatusResponse>::err({ErrorCode::INVALID_ARGUMENT, "bad status response"});
    }
    return Result<FileSystemRpcStatusResponse>::ok(FileSystemRpcStatusResponse{
        frame.value().requestId,
        frame.value().messageId,
        static_cast<FileSystemRpcStatus>(rawStatus),
    });
}

FLASHMEM Result<FileSystemRpcCapabilitiesResponse> FileSystemRpcCodec::decodeCapabilitiesResponse(
    const uint8_t* data,
    size_t size
) {
    auto frame = decodeFrame(data, size);
    if (!frame || frame.value().messageId != FileSystemRpcMessageId::CAPABILITIES_RESPONSE) {
        return Result<FileSystemRpcCapabilitiesResponse>::err(
            {ErrorCode::INVALID_ARGUMENT, "not capabilities response"}
        );
    }

    ByteReader reader(frame.value().payload, frame.value().payloadSize);
    uint8_t rawStatus = 0;
    FileSystemRpcCapabilitiesResponse response{};
    response.requestId = frame.value().requestId;
    if (!reader.readU8(rawStatus)) {
        return Result<FileSystemRpcCapabilitiesResponse>::err(
            {ErrorCode::INVALID_ARGUMENT, "bad capabilities response"}
        );
    }
    response.status = static_cast<FileSystemRpcStatus>(rawStatus);
    if (response.status != FileSystemRpcStatus::OK) {
        return Result<FileSystemRpcCapabilitiesResponse>::ok(response);
    }

    if (!reader.readU8(response.rpcSchema) ||
        !reader.readU16(response.maxChunkSize) ||
        !reader.readU16(response.responseBufferSize) ||
        !reader.readU8(response.maxListEntries) ||
        !reader.readU16(response.maxPathLength) ||
        !reader.readU32(response.featureFlags) ||
        reader.remaining() != 0) {
        return Result<FileSystemRpcCapabilitiesResponse>::err(
            {ErrorCode::INVALID_ARGUMENT, "bad capabilities payload"}
        );
    }
    return Result<FileSystemRpcCapabilitiesResponse>::ok(response);
}


}  // namespace core::protocol::filesystem

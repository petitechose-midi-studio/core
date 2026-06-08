#include "protocol/filesystem/FileSystemRpc.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

namespace core::protocol::filesystem {

namespace {

using oc::interface::DirectoryEntry;
using oc::interface::FileInfo;
using oc::interface::FileType;
using oc::type::Error;
using oc::type::ErrorCode;
using oc::type::Result;

constexpr const char* kErrorContextInvalidFrame = "invalid filesystem rpc frame";
constexpr const char* kErrorContextBufferTooSmall = "filesystem rpc buffer too small";

class ByteWriter {
public:
    FLASHMEM ByteWriter(uint8_t* data, size_t size) : data_(data), size_(size) {}

    FLASHMEM bool writeU8(uint8_t value) {
        if (remaining() < 1) return false;
        data_[offset_++] = value;
        return true;
    }

    FLASHMEM bool writeBool(bool value) {
        return writeU8(value ? 1 : 0);
    }

    FLASHMEM bool writeU16(uint16_t value) {
        if (remaining() < 2) return false;
        data_[offset_++] = static_cast<uint8_t>(value & 0xFF);
        data_[offset_++] = static_cast<uint8_t>((value >> 8) & 0xFF);
        return true;
    }

    FLASHMEM bool writeU32(uint32_t value) {
        if (remaining() < 4) return false;
        data_[offset_++] = static_cast<uint8_t>(value & 0xFF);
        data_[offset_++] = static_cast<uint8_t>((value >> 8) & 0xFF);
        data_[offset_++] = static_cast<uint8_t>((value >> 16) & 0xFF);
        data_[offset_++] = static_cast<uint8_t>((value >> 24) & 0xFF);
        return true;
    }

    FLASHMEM bool writeBytes(const uint8_t* data, size_t size) {
        if (!data && size > 0) return false;
        if (remaining() < size) return false;
        if (size > 0) {
            std::memcpy(data_ + offset_, data, size);
        }
        offset_ += size;
        return true;
    }

    FLASHMEM bool writeString(const char* value, size_t maxLength) {
        if (!value) return false;
        const size_t length = std::strlen(value);
        if (length > maxLength || length > UINT8_MAX) return false;
        return writeU8(static_cast<uint8_t>(length)) &&
               writeBytes(reinterpret_cast<const uint8_t*>(value), length);
    }

    FLASHMEM size_t position() const {
        return offset_;
    }

    FLASHMEM size_t remaining() const {
        return offset_ <= size_ ? size_ - offset_ : 0;
    }

    FLASHMEM uint8_t* cursor() {
        return data_ + offset_;
    }

    FLASHMEM bool advance(size_t size) {
        if (remaining() < size) return false;
        offset_ += size;
        return true;
    }

    FLASHMEM bool patchU8(size_t offset, uint8_t value) {
        if (offset >= size_) return false;
        data_[offset] = value;
        return true;
    }

private:
    uint8_t* data_ = nullptr;
    size_t size_ = 0;
    size_t offset_ = 0;
};

class ByteReader {
public:
    FLASHMEM ByteReader(const uint8_t* data, size_t size) : data_(data), remaining_(size) {}

    FLASHMEM bool readU8(uint8_t& value) {
        if (remaining_ < 1) return false;
        value = *data_++;
        --remaining_;
        return true;
    }

    FLASHMEM bool readBool(bool& value) {
        uint8_t raw = 0;
        if (!readU8(raw)) return false;
        value = raw != 0;
        return true;
    }

    FLASHMEM bool readU16(uint16_t& value) {
        if (remaining_ < 2) return false;
        value = static_cast<uint16_t>(data_[0]) |
                static_cast<uint16_t>(static_cast<uint16_t>(data_[1]) << 8);
        data_ += 2;
        remaining_ -= 2;
        return true;
    }

    FLASHMEM bool readU32(uint32_t& value) {
        if (remaining_ < 4) return false;
        value = static_cast<uint32_t>(data_[0]) |
                (static_cast<uint32_t>(data_[1]) << 8) |
                (static_cast<uint32_t>(data_[2]) << 16) |
                (static_cast<uint32_t>(data_[3]) << 24);
        data_ += 4;
        remaining_ -= 4;
        return true;
    }

    FLASHMEM bool readBytes(const uint8_t*& data, size_t size) {
        if (remaining_ < size) return false;
        data = data_;
        data_ += size;
        remaining_ -= size;
        return true;
    }

    FLASHMEM bool readString(char* out, size_t outSize, size_t maxLength) {
        if (!out || outSize == 0) return false;
        uint8_t length = 0;
        if (!readU8(length)) return false;
        if (length > maxLength || static_cast<size_t>(length) + 1 > outSize) return false;
        if (remaining_ < length) return false;
        if (length > 0) {
            std::memcpy(out, data_, length);
        }
        out[length] = '\0';
        data_ += length;
        remaining_ -= length;
        return true;
    }

    FLASHMEM size_t remaining() const {
        return remaining_;
    }

    FLASHMEM const uint8_t* current() const {
        return data_;
    }

private:
    const uint8_t* data_ = nullptr;
    size_t remaining_ = 0;
};

FLASHMEM FileSystemRpcStatus mapError(Error error) {
    switch (error.code) {
        case ErrorCode::OK:
            return FileSystemRpcStatus::OK;
        case ErrorCode::INVALID_ARGUMENT:
            return FileSystemRpcStatus::INVALID_ARGUMENT;
        case ErrorCode::RESOURCE_NOT_FOUND:
            return FileSystemRpcStatus::NOT_FOUND;
        case ErrorCode::HARDWARE_BUSY:
            return FileSystemRpcStatus::BUSY;
        case ErrorCode::RESOURCE_EXHAUSTED:
            return FileSystemRpcStatus::TOO_LARGE;
        case ErrorCode::INVALID_STATE:
            return FileSystemRpcStatus::INVALID_STATE;
        case ErrorCode::STORAGE_READ_FAILED:
        case ErrorCode::STORAGE_WRITE_FAILED:
        case ErrorCode::STORAGE_CORRUPT:
            return FileSystemRpcStatus::STORAGE_ERROR;
        default:
            return FileSystemRpcStatus::INVALID_STATE;
    }
}

FLASHMEM FileSystemRpcFileType mapFileType(FileType type) {
    switch (type) {
        case FileType::FILE:
            return FileSystemRpcFileType::FILE;
        case FileType::DIRECTORY:
            return FileSystemRpcFileType::DIRECTORY;
        case FileType::OTHER:
            return FileSystemRpcFileType::OTHER;
        case FileType::MISSING:
        default:
            return FileSystemRpcFileType::MISSING;
    }
}

FLASHMEM Result<size_t> bufferTooSmall() {
    return Result<size_t>::err({ErrorCode::RESOURCE_EXHAUSTED, kErrorContextBufferTooSmall});
}

FLASHMEM bool writeFrameHeader(ByteWriter& writer,
                               FileSystemRpcMessageId messageId,
                               uint16_t requestId) {
    const char* name = FileSystemRpcCodec::messageName(messageId);
    return writer.writeU8(static_cast<uint8_t>(messageId)) &&
           writer.writeString(name, UINT8_MAX) &&
           writer.writeU8(FILESYSTEM_RPC_SCHEMA) &&
           writer.writeU16(requestId);
}

FLASHMEM size_t encodeStatusOnly(FileSystemRpcMessageId messageId,
                                 uint16_t requestId,
                                 FileSystemRpcStatus status,
                                 uint8_t* out,
                                 size_t outSize) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, messageId, requestId) ||
        !writer.writeU8(static_cast<uint8_t>(status))) {
        return 0;
    }
    return writer.position();
}

FLASHMEM size_t encodeWriteResponse(FileSystemRpcMessageId messageId,
                                    uint16_t requestId,
                                    FileSystemRpcStatus status,
                                    uint16_t sessionId,
                                    uint16_t bytesWritten,
                                    uint8_t* out,
                                    size_t outSize) {
    ByteWriter writer(out, outSize);
    if (!writeFrameHeader(writer, messageId, requestId) ||
        !writer.writeU8(static_cast<uint8_t>(status)) ||
        !writer.writeU16(sessionId) ||
        !writer.writeU16(bytesWritten)) {
        return 0;
    }
    return writer.position();
}

FLASHMEM Result<void> readPath(ByteReader& reader, char* path, size_t pathSize) {
    if (!reader.readString(path, pathSize, oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) {
        return Result<void>::err({ErrorCode::INVALID_ARGUMENT, "invalid filesystem rpc path"});
    }
    return Result<void>::ok();
}

struct ListBuildContext {
    ByteWriter* writer = nullptr;
    uint16_t startIndex = 0;
    uint8_t maxEntries = 0;
    uint16_t visited = 0;
    uint8_t written = 0;
    bool hasMore = false;
};

FLASHMEM bool listVisitor(const DirectoryEntry& entry, void* context) {
    auto* list = static_cast<ListBuildContext*>(context);
    if (!list || !list->writer) return false;

    if (list->visited++ < list->startIndex) {
        return true;
    }

    if (list->written >= list->maxEntries) {
        list->hasMore = true;
        return false;
    }

    const size_t nameLength = std::strlen(entry.name);
    const size_t encodedSize = 1 + nameLength + 1 + 4 + 1;
    if (nameLength > UINT8_MAX || list->writer->remaining() < encodedSize) {
        list->hasMore = true;
        return false;
    }

    if (!list->writer->writeString(entry.name, oc::interface::FILESYSTEM_MAX_NAME_LENGTH) ||
        !list->writer->writeU8(static_cast<uint8_t>(mapFileType(entry.type))) ||
        !list->writer->writeU32(entry.sizeBytes) ||
        !list->writer->writeBool(entry.nameTruncated)) {
        list->hasMore = true;
        return false;
    }

    ++list->written;
    return true;
}

}  // namespace

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

FLASHMEM FileSystemRpcHandler::FileSystemRpcHandler(
    core::persistence::ProductFileService& files
) : FileSystemRpcHandler(files, Config{}) {}

FLASHMEM FileSystemRpcHandler::FileSystemRpcHandler(
    core::persistence::ProductFileService& files,
    Config config
) : files_(files), config_(config) {}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleFrame(
    const uint8_t* request,
    size_t requestSize,
    uint32_t nowMs,
    uint8_t* response,
    size_t responseSize
) {
    expireWriteSession_(nowMs);

    auto frame = FileSystemRpcCodec::decodeFrame(request, requestSize);
    if (!frame) {
        return encodeError_(0, FileSystemRpcStatus::INVALID_MESSAGE, response, responseSize);
    }
    if (frame.value().schema != FILESYSTEM_RPC_SCHEMA) {
        return encodeError_(frame.value().requestId, FileSystemRpcStatus::UNSUPPORTED, response, responseSize);
    }

    switch (frame.value().messageId) {
        case FileSystemRpcMessageId::STAT_REQUEST:
            return handleStat_(frame.value(), response, responseSize);
        case FileSystemRpcMessageId::CAPABILITIES_REQUEST:
            return handleCapabilities_(frame.value(), response, responseSize);
        case FileSystemRpcMessageId::LIST_REQUEST:
            return handleList_(frame.value(), response, responseSize);
        case FileSystemRpcMessageId::READ_REQUEST:
            return handleRead_(frame.value(), response, responseSize);
        case FileSystemRpcMessageId::WRITE_BEGIN_REQUEST:
            return handleWriteBegin_(frame.value(), nowMs, response, responseSize);
        case FileSystemRpcMessageId::WRITE_CHUNK_REQUEST:
            return handleWriteChunk_(frame.value(), nowMs, response, responseSize);
        case FileSystemRpcMessageId::WRITE_COMMIT_REQUEST:
            return handleWriteCommit_(frame.value(), response, responseSize);
        case FileSystemRpcMessageId::WRITE_ABORT_REQUEST:
            return handleWriteAbort_(frame.value(), response, responseSize);
        case FileSystemRpcMessageId::MKDIR_REQUEST:
            return handleMkdir_(frame.value(), response, responseSize);
        case FileSystemRpcMessageId::DELETE_REQUEST:
            return handleDelete_(frame.value(), response, responseSize);
        case FileSystemRpcMessageId::RENAME_REQUEST:
            return handleRename_(frame.value(), response, responseSize);
        default:
            return encodeError_(frame.value().requestId, FileSystemRpcStatus::INVALID_MESSAGE, response, responseSize);
    }
}

FLASHMEM void FileSystemRpcHandler::update(uint32_t nowMs) {
    expireWriteSession_(nowMs);
}

FLASHMEM bool FileSystemRpcHandler::hasActiveWriteSession() const {
    return writeSession_.active;
}

FLASHMEM void FileSystemRpcHandler::abortWriteSession() {
    if (writeSession_.active) {
        files_.abortWrite();
        (void)files_.remove(writeSession_.tmpPath);
    }
    clearWriteSession_();
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleCapabilities_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    if (frame.payloadSize != 0) {
        return encodeError_(frame.requestId, FileSystemRpcStatus::INVALID_ARGUMENT, response, responseSize);
    }

    ByteWriter writer(response, responseSize);
    constexpr uint32_t features =
        FILESYSTEM_RPC_FEATURE_CAPABILITIES |
        FILESYSTEM_RPC_FEATURE_WRITE_SESSIONS |
        FILESYSTEM_RPC_FEATURE_FILE_MANAGEMENT;
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::CAPABILITIES_RESPONSE, frame.requestId) ||
        !writer.writeU8(static_cast<uint8_t>(FileSystemRpcStatus::OK)) ||
        !writer.writeU8(FILESYSTEM_RPC_SCHEMA) ||
        !writer.writeU16(static_cast<uint16_t>(FILESYSTEM_RPC_MAX_CHUNK_SIZE)) ||
        !writer.writeU16(static_cast<uint16_t>(FILESYSTEM_RPC_RESPONSE_BUFFER_SIZE)) ||
        !writer.writeU8(FILESYSTEM_RPC_MAX_LIST_ENTRIES) ||
        !writer.writeU16(static_cast<uint16_t>(oc::interface::FILESYSTEM_MAX_PATH_LENGTH)) ||
        !writer.writeU32(features)) {
        return bufferTooSmall();
    }
    return Result<size_t>::ok(writer.position());
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleStat_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    char path[PATH_BUFFER_SIZE] = {};
    ByteReader reader(frame.payload, frame.payloadSize);
    auto pathResult = readPath(reader, path, sizeof(path));
    if (!pathResult || reader.remaining() != 0) {
        return encodeError_(frame.requestId, FileSystemRpcStatus::INVALID_ARGUMENT, response, responseSize);
    }

    auto stat = files_.stat(path);
    if (!stat) {
        const auto status = mapError(stat.error());
        const size_t size = encodeStatusOnly(
            FileSystemRpcMessageId::STAT_RESPONSE,
            frame.requestId,
            status,
            response,
            responseSize
        );
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }

    ByteWriter writer(response, responseSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::STAT_RESPONSE, frame.requestId) ||
        !writer.writeU8(static_cast<uint8_t>(FileSystemRpcStatus::OK)) ||
        !writer.writeU8(static_cast<uint8_t>(mapFileType(stat.value().type))) ||
        !writer.writeU32(stat.value().sizeBytes)) {
        return bufferTooSmall();
    }
    return Result<size_t>::ok(writer.position());
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleList_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    uint16_t startIndex = 0;
    uint8_t maxEntries = 0;
    char path[PATH_BUFFER_SIZE] = {};

    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU16(startIndex) ||
        !reader.readU8(maxEntries) ||
        !readPath(reader, path, sizeof(path)) ||
        reader.remaining() != 0) {
        return encodeError_(frame.requestId, FileSystemRpcStatus::INVALID_ARGUMENT, response, responseSize);
    }

    if (maxEntries == 0 || maxEntries > FILESYSTEM_RPC_MAX_LIST_ENTRIES) {
        maxEntries = FILESYSTEM_RPC_MAX_LIST_ENTRIES;
    }

    ByteWriter writer(response, responseSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::LIST_RESPONSE, frame.requestId) ||
        !writer.writeU8(static_cast<uint8_t>(FileSystemRpcStatus::OK)) ||
        !writer.writeU16(startIndex)) {
        return bufferTooSmall();
    }
    const size_t countOffset = writer.position();
    if (!writer.writeU8(0)) return bufferTooSmall();
    const size_t hasMoreOffset = writer.position();
    if (!writer.writeU8(0)) return bufferTooSmall();

    ListBuildContext context{&writer, startIndex, maxEntries, 0, 0, false};
    auto list = files_.list(path, listVisitor, &context);
    if (!list) {
        const size_t size = encodeStatusOnly(
            FileSystemRpcMessageId::LIST_RESPONSE,
            frame.requestId,
            mapError(list.error()),
            response,
            responseSize
        );
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }

    writer.patchU8(countOffset, context.written);
    writer.patchU8(hasMoreOffset, context.hasMore ? 1 : 0);
    return Result<size_t>::ok(writer.position());
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleRead_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    uint32_t offset = 0;
    uint16_t requestedSize = 0;
    char path[PATH_BUFFER_SIZE] = {};

    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU32(offset) ||
        !reader.readU16(requestedSize) ||
        !readPath(reader, path, sizeof(path)) ||
        reader.remaining() != 0 ||
        requestedSize > FILESYSTEM_RPC_MAX_CHUNK_SIZE) {
        return encodeError_(frame.requestId, FileSystemRpcStatus::INVALID_ARGUMENT, response, responseSize);
    }

    ByteWriter writer(response, responseSize);
    if (!writeFrameHeader(writer, FileSystemRpcMessageId::READ_RESPONSE, frame.requestId) ||
        !writer.writeU8(static_cast<uint8_t>(FileSystemRpcStatus::OK)) ||
        !writer.writeU32(offset)) {
        return bufferTooSmall();
    }
    const size_t countOffset = writer.position();
    if (!writer.writeU16(0) || writer.remaining() < requestedSize) {
        return bufferTooSmall();
    }

    uint8_t* readTarget = writer.cursor();
    auto read = files_.read(path, offset, readTarget, requestedSize);
    if (!read) {
        const size_t size = encodeStatusOnly(
            FileSystemRpcMessageId::READ_RESPONSE,
            frame.requestId,
            mapError(read.error()),
            response,
            responseSize
        );
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }

    const auto bytesRead = static_cast<uint16_t>(read.value());
    if (!writer.advance(bytesRead)) {
        return bufferTooSmall();
    }
    response[countOffset] = static_cast<uint8_t>(bytesRead & 0xFF);
    response[countOffset + 1] = static_cast<uint8_t>((bytesRead >> 8) & 0xFF);
    return Result<size_t>::ok(writer.position());
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleWriteBegin_(
    const FileSystemRpcFrame& frame,
    uint32_t nowMs,
    uint8_t* response,
    size_t responseSize
) {
    uint16_t sessionId = 0;
    uint32_t expectedSize = 0;
    char path[PATH_BUFFER_SIZE] = {};

    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU16(sessionId) ||
        !reader.readU32(expectedSize) ||
        !readPath(reader, path, sizeof(path)) ||
        reader.remaining() != 0) {
        const size_t size = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            sessionId,
            0,
            response,
            responseSize
        );
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }

    if (writeSession_.active) {
        const size_t size = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::BUSY,
            sessionId,
            0,
            response,
            responseSize
        );
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }

    clearWriteSession_();
    if (!copySessionPath_(path, sessionId)) {
        const size_t size = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            sessionId,
            0,
            response,
            responseSize
        );
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }

    (void)files_.remove(writeSession_.tmpPath);
    auto begin = files_.beginWrite(writeSession_.tmpPath, expectedSize);
    if (!begin) {
        const size_t size = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE,
            frame.requestId,
            mapError(begin.error()),
            sessionId,
            0,
            response,
            responseSize
        );
        clearWriteSession_();
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }

    writeSession_.active = true;
    writeSession_.sessionId = sessionId;
    writeSession_.expectedSize = expectedSize;
    writeSession_.writtenBytes = 0;
    writeSession_.lastActivityMs = nowMs;

    const size_t size = encodeWriteResponse(
        FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE,
        frame.requestId,
        FileSystemRpcStatus::OK,
        sessionId,
        0,
        response,
        responseSize
    );
    return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleWriteChunk_(
    const FileSystemRpcFrame& frame,
    uint32_t nowMs,
    uint8_t* response,
    size_t responseSize
) {
    uint16_t sessionId = 0;
    uint32_t offset = 0;
    uint16_t size = 0;
    const uint8_t* chunk = nullptr;

    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU16(sessionId) ||
        !reader.readU32(offset) ||
        !reader.readU16(size) ||
        size > FILESYSTEM_RPC_MAX_CHUNK_SIZE ||
        !reader.readBytes(chunk, size) ||
        reader.remaining() != 0) {
        const size_t encoded = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_CHUNK_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            sessionId,
            0,
            response,
            responseSize
        );
        return encoded > 0 ? Result<size_t>::ok(encoded) : bufferTooSmall();
    }

    FileSystemRpcStatus status = FileSystemRpcStatus::OK;
    uint16_t bytesWritten = 0;
    if (!writeSession_.active || writeSession_.sessionId != sessionId) {
        status = FileSystemRpcStatus::INVALID_STATE;
    } else if (offset != writeSession_.writtenBytes ||
               offset + size > writeSession_.expectedSize) {
        status = FileSystemRpcStatus::INVALID_ARGUMENT;
    } else {
        auto written = files_.appendWrite(chunk, size);
        if (!written || written.value() != size) {
            status = written ? FileSystemRpcStatus::STORAGE_ERROR : mapError(written.error());
            files_.abortWrite();
            (void)files_.remove(writeSession_.tmpPath);
            clearWriteSession_();
        } else {
            bytesWritten = static_cast<uint16_t>(written.value());
            writeSession_.writtenBytes += bytesWritten;
            writeSession_.lastActivityMs = nowMs;
        }
    }

    const size_t encoded = encodeWriteResponse(
        FileSystemRpcMessageId::WRITE_CHUNK_RESPONSE,
        frame.requestId,
        status,
        sessionId,
        bytesWritten,
        response,
        responseSize
    );
    return encoded > 0 ? Result<size_t>::ok(encoded) : bufferTooSmall();
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleWriteCommit_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    uint16_t sessionId = 0;
    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU16(sessionId) || reader.remaining() != 0) {
        const size_t encoded = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_COMMIT_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            sessionId,
            0,
            response,
            responseSize
        );
        return encoded > 0 ? Result<size_t>::ok(encoded) : bufferTooSmall();
    }

    FileSystemRpcStatus status = FileSystemRpcStatus::OK;
    bool writeFinished = false;
    if (!writeSession_.active || writeSession_.sessionId != sessionId) {
        status = FileSystemRpcStatus::INVALID_STATE;
    } else if (writeSession_.writtenBytes != writeSession_.expectedSize) {
        status = FileSystemRpcStatus::INVALID_STATE;
    } else {
        auto finish = files_.finishWrite();
        if (!finish) {
            status = mapError(finish.error());
            files_.abortWrite();
            (void)files_.remove(writeSession_.tmpPath);
            clearWriteSession_();
        } else {
            writeFinished = true;
            bool targetWasBackedUp = false;
            auto existing = files_.stat(writeSession_.finalPath);
            if (!existing && existing.error().code != ErrorCode::RESOURCE_NOT_FOUND) {
                status = mapError(existing.error());
            } else if (existing && existing.value().exists()) {
                (void)files_.remove(writeSession_.backupPath);
                auto backup = files_.rename(writeSession_.finalPath, writeSession_.backupPath);
                if (!backup) {
                    status = mapError(backup.error());
                } else {
                    targetWasBackedUp = true;
                }
            }
            if (status == FileSystemRpcStatus::OK) {
                auto rename = files_.rename(writeSession_.tmpPath, writeSession_.finalPath);
                if (!rename) {
                    status = mapError(rename.error());
                    if (targetWasBackedUp) {
                        (void)files_.rename(writeSession_.backupPath, writeSession_.finalPath);
                    }
                } else if (targetWasBackedUp) {
                    (void)files_.remove(writeSession_.backupPath);
                }
            }
        }
    }

    if (status == FileSystemRpcStatus::OK) {
        clearWriteSession_();
    } else if (writeFinished) {
        (void)files_.remove(writeSession_.tmpPath);
        (void)files_.remove(writeSession_.backupPath);
        clearWriteSession_();
    }

    const size_t encoded = encodeWriteResponse(
        FileSystemRpcMessageId::WRITE_COMMIT_RESPONSE,
        frame.requestId,
        status,
        sessionId,
        0,
        response,
        responseSize
    );
    return encoded > 0 ? Result<size_t>::ok(encoded) : bufferTooSmall();
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleWriteAbort_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    uint16_t sessionId = 0;
    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU16(sessionId) || reader.remaining() != 0) {
        const size_t encoded = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_ABORT_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            sessionId,
            0,
            response,
            responseSize
        );
        return encoded > 0 ? Result<size_t>::ok(encoded) : bufferTooSmall();
    }

    FileSystemRpcStatus status = FileSystemRpcStatus::OK;
    if (!writeSession_.active || writeSession_.sessionId != sessionId) {
        status = FileSystemRpcStatus::INVALID_STATE;
    } else {
        files_.abortWrite();
        (void)files_.remove(writeSession_.tmpPath);
        (void)files_.remove(writeSession_.backupPath);
        clearWriteSession_();
    }

    const size_t encoded = encodeWriteResponse(
        FileSystemRpcMessageId::WRITE_ABORT_RESPONSE,
        frame.requestId,
        status,
        sessionId,
        0,
        response,
        responseSize
    );
    return encoded > 0 ? Result<size_t>::ok(encoded) : bufferTooSmall();
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleMkdir_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    char path[PATH_BUFFER_SIZE] = {};
    ByteReader reader(frame.payload, frame.payloadSize);
    FileSystemRpcStatus status = FileSystemRpcStatus::OK;
    if (!readPath(reader, path, sizeof(path)) || reader.remaining() != 0) {
        status = FileSystemRpcStatus::INVALID_ARGUMENT;
    } else {
        auto result = files_.createDirectory(path);
        if (!result) {
            status = mapError(result.error());
        }
    }

    const size_t encoded = encodeStatusOnly(
        FileSystemRpcMessageId::MKDIR_RESPONSE,
        frame.requestId,
        status,
        response,
        responseSize
    );
    return encoded > 0 ? Result<size_t>::ok(encoded) : bufferTooSmall();
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleDelete_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    bool recursive = false;
    char path[PATH_BUFFER_SIZE] = {};
    ByteReader reader(frame.payload, frame.payloadSize);
    FileSystemRpcStatus status = FileSystemRpcStatus::OK;
    if (!reader.readBool(recursive) ||
        !readPath(reader, path, sizeof(path)) ||
        reader.remaining() != 0) {
        status = FileSystemRpcStatus::INVALID_ARGUMENT;
    } else {
        const auto mode = recursive
            ? oc::interface::RemoveMode::RECURSIVE
            : oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY;
        auto result = files_.remove(path, mode);
        if (!result) {
            status = mapError(result.error());
        }
    }

    const size_t encoded = encodeStatusOnly(
        FileSystemRpcMessageId::DELETE_RESPONSE,
        frame.requestId,
        status,
        response,
        responseSize
    );
    return encoded > 0 ? Result<size_t>::ok(encoded) : bufferTooSmall();
}

FLASHMEM Result<size_t> FileSystemRpcHandler::handleRename_(
    const FileSystemRpcFrame& frame,
    uint8_t* response,
    size_t responseSize
) {
    char fromPath[PATH_BUFFER_SIZE] = {};
    char toPath[PATH_BUFFER_SIZE] = {};
    ByteReader reader(frame.payload, frame.payloadSize);
    FileSystemRpcStatus status = FileSystemRpcStatus::OK;
    if (!readPath(reader, fromPath, sizeof(fromPath)) ||
        !readPath(reader, toPath, sizeof(toPath)) ||
        reader.remaining() != 0) {
        status = FileSystemRpcStatus::INVALID_ARGUMENT;
    } else {
        auto result = files_.rename(fromPath, toPath);
        if (!result) {
            status = mapError(result.error());
        }
    }

    const size_t encoded = encodeStatusOnly(
        FileSystemRpcMessageId::RENAME_RESPONSE,
        frame.requestId,
        status,
        response,
        responseSize
    );
    return encoded > 0 ? Result<size_t>::ok(encoded) : bufferTooSmall();
}

FLASHMEM Result<size_t> FileSystemRpcHandler::encodeError_(
    uint16_t requestId,
    FileSystemRpcStatus status,
    uint8_t* response,
    size_t responseSize
) const {
    const size_t size = encodeStatusOnly(
        FileSystemRpcMessageId::ERROR_RESPONSE,
        requestId,
        status,
        response,
        responseSize
    );
    return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
}

FLASHMEM void FileSystemRpcHandler::expireWriteSession_(uint32_t nowMs) {
    if (!writeSession_.active) {
        return;
    }
    if (static_cast<uint32_t>(nowMs - writeSession_.lastActivityMs) <=
        config_.writeSessionTimeoutMs) {
        return;
    }
    abortWriteSession();
}

FLASHMEM void FileSystemRpcHandler::clearWriteSession_() {
    writeSession_ = {};
}

FLASHMEM bool FileSystemRpcHandler::copySessionPath_(const char* path, uint16_t sessionId) {
    if (!path || path[0] == '\0') return false;
    const size_t length = std::strlen(path);
    if (length >= sizeof(writeSession_.finalPath)) return false;
    std::memcpy(writeSession_.finalPath, path, length + 1);

    const int written = std::snprintf(
        writeSession_.tmpPath,
        sizeof(writeSession_.tmpPath),
        "tmp/rpc-write-%04X.tmp",
        static_cast<unsigned>(sessionId)
    );
    if (written <= 0 || static_cast<size_t>(written) >= sizeof(writeSession_.tmpPath)) {
        return false;
    }

    const int backupWritten = std::snprintf(
        writeSession_.backupPath,
        sizeof(writeSession_.backupPath),
        "tmp/rpc-backup-%04X.tmp",
        static_cast<unsigned>(sessionId)
    );
    return backupWritten > 0 &&
           static_cast<size_t>(backupWritten) < sizeof(writeSession_.backupPath);
}

FLASHMEM FileSystemRpcEndpoint::FileSystemRpcEndpoint(
    oc::interface::ITransport& transport,
    core::persistence::ProductFileService& files,
    NowProvider nowProvider,
    FileSystemRpcHandler::Config handlerConfig
) : transport_(transport),
    nowProvider_(nowProvider),
    handler_(files, handlerConfig) {}

FLASHMEM FileSystemRpcEndpoint::~FileSystemRpcEndpoint() {
    end();
}

FLASHMEM void FileSystemRpcEndpoint::begin() {
    transport_.setOnReceive([this](const uint8_t* data, size_t size) {
        handleReceive_(data, size);
    });
    active_ = true;
}

FLASHMEM void FileSystemRpcEndpoint::end() {
    if (!active_) {
        return;
    }
    transport_.setOnReceive({});
    handler_.abortWriteSession();
    active_ = false;
}

FLASHMEM bool FileSystemRpcEndpoint::active() const {
    return active_;
}

FLASHMEM void FileSystemRpcEndpoint::handleReceive_(const uint8_t* data, size_t size) {
    if (!data || size == 0) {
        return;
    }
    const uint8_t messageId = data[0];
    if (!FileSystemRpcCodec::isFileSystemRequestId(messageId)) {
        return;
    }

    const uint32_t nowMs = nowProvider_ ? nowProvider_() : 0;
    auto response = handler_.handleFrame(
        data,
        size,
        nowMs,
        response_,
        sizeof(response_)
    );
    if (!response || response.value() == 0) {
        return;
    }
    transport_.send(response_, response.value());
}

}  // namespace core::protocol::filesystem

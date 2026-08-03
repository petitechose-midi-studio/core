#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/AtomicProductFile.hpp"

namespace core::protocol::filesystem {

using oc::interface::DirectoryEntry;
using oc::type::Result;
using internal::ByteReader;
using internal::ByteWriter;
using internal::bufferTooSmall;
using internal::encodeStatusOnly;
using internal::mapError;
using internal::mapFileType;
using internal::readPath;
using internal::writeFrameHeader;

namespace {

FLASHMEM bool isRecoverySafeRequest(FileSystemRpcMessageId messageId) {
    return messageId == FileSystemRpcMessageId::CAPABILITIES_REQUEST;
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

    updateConditionalRecovery_(nowMs);
    if (conditionalRecoveryState_ == FileSystemRpcConditionalRecoveryState::BLOCKED &&
        !isRecoverySafeRequest(frame.value().messageId)) {
        const auto blockedStatus =
            conditionalRecoveryStatus_ == FileSystemRpcStatus::UNSUPPORTED ||
            conditionalRecoveryStatus_ == FileSystemRpcStatus::TOO_LARGE
            ? conditionalRecoveryStatus_
            : (files_.storageState() == core::persistence::ProductStorageState::ABSENT
                ? FileSystemRpcStatus::STORAGE_ERROR
                : FileSystemRpcStatus::BUSY);
        return encodeError_(
            frame.value().requestId,
            blockedStatus,
            response,
            responseSize
        );
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
        case FileSystemRpcMessageId::CONDITIONAL_REPLACE_REQUEST:
            return handleConditionalReplace_(frame.value(), nowMs, response, responseSize);
        case FileSystemRpcMessageId::CONDITIONAL_DELETE_REQUEST:
            return handleConditionalDelete_(frame.value(), nowMs, response, responseSize);
        default:
            return encodeError_(frame.value().requestId, FileSystemRpcStatus::INVALID_MESSAGE, response, responseSize);
    }
}

void FileSystemRpcHandler::update(uint32_t nowMs) {
    expireWriteSession_(nowMs);
    updateConditionalRecovery_(nowMs);
}

bool FileSystemRpcHandler::hasActiveWriteSession() const {
    return writeSession_.lease.valid() &&
           files_.owns(
               writeSession_.lease,
               core::persistence::ProductMutationOwner::FILESYSTEM_RPC
           );
}

FLASHMEM void FileSystemRpcHandler::abortWriteSession() {
    if (writeSession_.lease.valid() && files_.owns(writeSession_.lease)) {
        (void)files_.abortWrite(writeSession_.lease);
        (void)core::persistence::deleteProductFileIfExists(
            files_,
            writeSession_.lease,
            writeSession_.tmpPath
        );
    }
    (void)releaseWriteSession_();
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
        FILESYSTEM_RPC_FEATURE_FILE_MANAGEMENT |
        FILESYSTEM_RPC_FEATURE_CONDITIONAL_MUTATIONS;
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

}  // namespace core::protocol::filesystem

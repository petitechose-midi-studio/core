#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <config/PlatformCompat.hpp>

namespace core::protocol::filesystem {

using oc::type::Result;
using internal::ByteReader;
using internal::bufferTooSmall;
using internal::encodeStatusOnly;
using internal::mapError;
using internal::readPath;

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


}  // namespace core::protocol::filesystem

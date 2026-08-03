#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <utility>

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
    } else if (internal::isProtocolReservedPath(files_, path)) {
        status = FileSystemRpcStatus::INVALID_ARGUMENT;
    } else {
        auto acquired = files_.acquireMutation(
            core::persistence::ProductMutationOwner::FILESYSTEM_RPC
        );
        if (!acquired) {
            status = mapError(acquired.error());
        } else {
            auto lease = std::move(acquired.value());
            auto result = files_.createDirectory(lease, path);
            if (!result) status = mapError(result.error());
            auto released = files_.releaseMutation(lease);
            if (status == FileSystemRpcStatus::OK && !released) {
                status = mapError(released.error());
            }
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
    } else if (internal::isProtocolReservedPath(files_, path)) {
        status = FileSystemRpcStatus::INVALID_ARGUMENT;
    } else {
        const auto mode = recursive
            ? oc::interface::RemoveMode::RECURSIVE
            : oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY;
        auto acquired = files_.acquireMutation(
            core::persistence::ProductMutationOwner::FILESYSTEM_RPC
        );
        if (!acquired) {
            status = mapError(acquired.error());
        } else {
            auto lease = std::move(acquired.value());
            auto result = files_.remove(lease, path, mode);
            if (!result) status = mapError(result.error());
            auto released = files_.releaseMutation(lease);
            if (status == FileSystemRpcStatus::OK && !released) {
                status = mapError(released.error());
            }
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
    } else if (internal::isProtocolReservedPath(files_, fromPath) ||
               internal::isProtocolReservedPath(files_, toPath)) {
        status = FileSystemRpcStatus::INVALID_ARGUMENT;
    } else {
        auto acquired = files_.acquireMutation(
            core::persistence::ProductMutationOwner::FILESYSTEM_RPC
        );
        if (!acquired) {
            status = mapError(acquired.error());
        } else {
            auto lease = std::move(acquired.value());
            auto result = files_.rename(lease, fromPath, toPath);
            if (!result) status = mapError(result.error());
            auto released = files_.releaseMutation(lease);
            if (status == FileSystemRpcStatus::OK && !released) {
                status = mapError(released.error());
            }
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

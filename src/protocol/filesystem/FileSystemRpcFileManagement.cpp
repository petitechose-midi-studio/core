#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <utility>

#include <config/PlatformCompat.hpp>

#include "persistence/ProductTreeCleanupPlan.hpp"

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
        if (recursive) {
            // Firmware dispatches recursive requests through the retained
            // endpoint continuation below. The direct compatibility handler
            // must never re-enter the backend's node-unbounded recursion.
            status = FileSystemRpcStatus::BUSY;
        } else {
            auto acquired = files_.acquireMutation(
                core::persistence::ProductMutationOwner::FILESYSTEM_RPC
            );
            if (!acquired) {
                status = mapError(acquired.error());
            } else {
                auto lease = std::move(acquired.value());
                auto result = files_.remove(
                    lease,
                    path,
                    oc::interface::RemoveMode::FILE_OR_EMPTY_DIRECTORY
                );
                if (!result) status = mapError(result.error());
                auto released = files_.releaseMutation(lease);
                if (status == FileSystemRpcStatus::OK && !released) {
                    status = mapError(released.error());
                }
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

FLASHMEM Result<size_t> FileSystemRpcHandler::beginCooperativeRecursiveDelete_(
    const FileSystemRpcFrame& frame,
    core::persistence::ProductTreeCleanupPlan& plan,
    uint8_t* response,
    size_t responseSize
) {
    bool recursive = false;
    char path[PATH_BUFFER_SIZE] = {};
    ByteReader reader(frame.payload, frame.payloadSize);
    FileSystemRpcStatus status = FileSystemRpcStatus::OK;
    if (!reader.readBool(recursive) ||
        !readPath(reader, path, sizeof(path)) ||
        reader.remaining() != 0U) {
        status = FileSystemRpcStatus::INVALID_ARGUMENT;
    } else if (internal::isProtocolReservedPath(files_, path)) {
        status = FileSystemRpcStatus::INVALID_ARGUMENT;
    } else if (!recursive) {
        return handleDelete_(frame, response, responseSize);
    } else {
        const auto begun = plan.beginDelete(files_, path);
        if (!begun) status = mapError(begun.error());
    }

    if (status == FileSystemRpcStatus::OK) {
        // Admission owns no filesystem primitive. The first bounded cleanup
        // step runs on the next foreground turn under the same job token.
        return Result<size_t>::ok(0U);
    }
    const size_t encoded = encodeStatusOnly(
        FileSystemRpcMessageId::DELETE_RESPONSE,
        frame.requestId,
        status,
        response,
        responseSize
    );
    return encoded > 0U ? Result<size_t>::ok(encoded) : bufferTooSmall();
}

FLASHMEM Result<size_t> FileSystemRpcHandler::advanceCooperativeRecursiveDelete_(
    core::persistence::ProductTreeCleanupPlan& plan,
    uint16_t requestId,
    uint8_t* response,
    size_t responseSize,
    core::persistence::ProductPersistenceWorkMeasurement* measurement
) {
    if (!plan.advanceDelete(files_, measurement)) {
        return Result<size_t>::ok(0U);
    }
    const auto status = plan.completed()
        ? FileSystemRpcStatus::OK
        : mapError(plan.error());
    const size_t encoded = encodeStatusOnly(
        FileSystemRpcMessageId::DELETE_RESPONSE,
        requestId,
        status,
        response,
        responseSize
    );
    return encoded > 0U ? Result<size_t>::ok(encoded) : bufferTooSmall();
}

FLASHMEM void FileSystemRpcHandler::cancelCooperativeRecursiveDelete_(
    core::persistence::ProductTreeCleanupPlan& plan
) {
    plan.cancelDelete(files_);
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

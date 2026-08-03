#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <cstdio>
#include <cstring>
#include <utility>

#include <config/PlatformCompat.hpp>

#include "persistence/AtomicProductFile.hpp"
#include "persistence/ProductFileCommitPlan.hpp"

namespace core::protocol::filesystem {

using oc::type::ErrorCode;
using oc::type::Result;
using internal::ByteReader;
using internal::bufferTooSmall;
using internal::encodeWriteResponse;
using internal::mapError;
using internal::readPath;

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

    if (expectedSize > FILESYSTEM_RPC_MAX_UPLOAD_SIZE) {
        const size_t size = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::TOO_LARGE,
            sessionId,
            0,
            response,
            responseSize
        );
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }

    if (hasActiveWriteSession()) {
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

    if (internal::isProtocolReservedPath(files_, path)) {
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

    auto acquired = files_.acquireMutation(
        core::persistence::ProductMutationOwner::FILESYSTEM_RPC
    );
    if (!acquired) {
        const size_t size = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE,
            frame.requestId,
            mapError(acquired.error()),
            sessionId,
            0,
            response,
            responseSize
        );
        clearWriteSession_();
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }
    writeSession_.lease = std::move(acquired.value());

    auto deletedTmp = core::persistence::deleteProductFileIfExists(
        files_,
        writeSession_.lease,
        writeSession_.tmpPath
    );
    if (!deletedTmp) {
        const auto status = mapError(deletedTmp.error());
        (void)releaseWriteSession_();
        const size_t size = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE,
            frame.requestId,
            status,
            sessionId,
            0,
            response,
            responseSize
        );
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }

    auto begin = files_.beginWrite(writeSession_.lease, writeSession_.tmpPath, expectedSize);
    if (!begin) {
        const auto status = mapError(begin.error());
        (void)releaseWriteSession_();
        const size_t size = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE,
            frame.requestId,
            status,
            sessionId,
            0,
            response,
            responseSize
        );
        return size > 0 ? Result<size_t>::ok(size) : bufferTooSmall();
    }

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
    if (!hasActiveWriteSession() || writeSession_.sessionId != sessionId) {
        status = FileSystemRpcStatus::INVALID_STATE;
    } else if (offset != writeSession_.writtenBytes ||
               offset > writeSession_.expectedSize ||
               static_cast<uint32_t>(size) > writeSession_.expectedSize - offset) {
        status = FileSystemRpcStatus::INVALID_ARGUMENT;
    } else {
        auto written = files_.appendWrite(writeSession_.lease, chunk, size);
        if (!written || written.value() != size) {
            status = written ? FileSystemRpcStatus::STORAGE_ERROR : mapError(written.error());
            (void)files_.abortWrite(writeSession_.lease);
            (void)core::persistence::deleteProductFileIfExists(
                files_,
                writeSession_.lease,
                writeSession_.tmpPath
            );
            (void)releaseWriteSession_();
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
    core::persistence::ProductFileCommitPlan plan{};
    uint16_t sessionId = 0U;
    auto result = beginCooperativeWriteCommit_(
        frame,
        plan,
        sessionId,
        response,
        responseSize
    );
    while (result && result.value() == 0U && plan.active()) {
        result = advanceCooperativeWriteCommit_(
            plan,
            frame.requestId,
            sessionId,
            response,
            responseSize
        );
    }
    return result;
}

FLASHMEM Result<size_t> FileSystemRpcHandler::beginCooperativeWriteCommit_(
    const FileSystemRpcFrame& frame,
    core::persistence::ProductFileCommitPlan& plan,
    uint16_t& sessionId,
    uint8_t* response,
    size_t responseSize
) {
    sessionId = 0U;
    ByteReader reader(frame.payload, frame.payloadSize);
    if (!reader.readU16(sessionId) || reader.remaining() != 0U) {
        const size_t encoded = encodeWriteResponse(
            FileSystemRpcMessageId::WRITE_COMMIT_RESPONSE,
            frame.requestId,
            FileSystemRpcStatus::INVALID_ARGUMENT,
            sessionId,
            0U,
            response,
            responseSize
        );
        return encoded > 0U ? Result<size_t>::ok(encoded) : bufferTooSmall();
    }

    FileSystemRpcStatus status = FileSystemRpcStatus::OK;
    if (!hasActiveWriteSession() || writeSession_.sessionId != sessionId) {
        status = FileSystemRpcStatus::INVALID_STATE;
    } else if (writeSession_.writtenBytes != writeSession_.expectedSize) {
        status = FileSystemRpcStatus::INVALID_STATE;
    } else {
        auto finished = files_.finishWrite(writeSession_.lease);
        if (!finished) {
            status = mapError(finished.error());
            (void)files_.abortWrite(writeSession_.lease);
        } else {
            auto begun = plan.begin(
                files_,
                writeSession_.lease,
                writeSession_.finalPath,
                writeSession_.backupPath,
                writeSession_.tmpPath,
                writeSession_.expectedSize
            );
            if (begun) return Result<size_t>::ok(0U);
            status = mapError(begun.error());
        }

        if (files_.owns(writeSession_.lease) &&
            !files_.recoveryRequired(writeSession_.lease)) {
            (void)core::persistence::deleteProductFileIfExists(
                files_,
                writeSession_.lease,
                writeSession_.tmpPath
            );
        }
        auto released = releaseWriteSession_();
        if (status == FileSystemRpcStatus::OK && !released) {
            status = mapError(released.error());
        }
    }

    const size_t encoded = encodeWriteResponse(
        FileSystemRpcMessageId::WRITE_COMMIT_RESPONSE,
        frame.requestId,
        status,
        sessionId,
        0U,
        response,
        responseSize
    );
    return encoded > 0U ? Result<size_t>::ok(encoded) : bufferTooSmall();
}

FLASHMEM Result<size_t> FileSystemRpcHandler::advanceCooperativeWriteCommit_(
    core::persistence::ProductFileCommitPlan& plan,
    uint16_t requestId,
    uint16_t sessionId,
    uint8_t* response,
    size_t responseSize
) {
    FileSystemRpcStatus status = FileSystemRpcStatus::OK;
    if (!hasActiveWriteSession() || writeSession_.sessionId != sessionId) {
        status = FileSystemRpcStatus::INVALID_STATE;
    } else {
        auto advanced = plan.advance(files_, writeSession_.lease);
        if (advanced && !advanced.value()) return Result<size_t>::ok(0U);
        if (!advanced) {
            status = mapError(advanced.error());
            if (plan.requiresRecoveryOnFailure() && files_.owns(writeSession_.lease)) {
                (void)files_.requireRecovery(
                    writeSession_.lease,
                    advanced.error().code
                );
            } else if (files_.owns(writeSession_.lease)) {
                (void)core::persistence::deleteProductFileIfExists(
                    files_,
                    writeSession_.lease,
                    writeSession_.tmpPath
                );
            }
        }

        auto released = releaseWriteSession_();
        if (status == FileSystemRpcStatus::OK && !released) {
            status = mapError(released.error());
        }
    }
    plan.reset();

    const size_t encoded = encodeWriteResponse(
        FileSystemRpcMessageId::WRITE_COMMIT_RESPONSE,
        requestId,
        status,
        sessionId,
        0U,
        response,
        responseSize
    );
    return encoded > 0U ? Result<size_t>::ok(encoded) : bufferTooSmall();
}

FLASHMEM void FileSystemRpcHandler::cancelCooperativeWriteCommit_(
    core::persistence::ProductFileCommitPlan& plan
) {
    if (writeSession_.lease.valid() && files_.owns(writeSession_.lease)) {
        if (plan.mapped() || plan.requiresRecoveryOnFailure()) {
            (void)files_.requireRecovery(
                writeSession_.lease,
                ErrorCode::STORAGE_WRITE_FAILED
            );
            (void)releaseWriteSession_();
        } else {
            abortWriteSession();
        }
    } else {
        (void)releaseWriteSession_();
    }
    plan.reset();
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
    if (!hasActiveWriteSession() || writeSession_.sessionId != sessionId) {
        status = FileSystemRpcStatus::INVALID_STATE;
    } else {
        (void)files_.abortWrite(writeSession_.lease);
        (void)core::persistence::deleteProductFileIfExists(
            files_,
            writeSession_.lease,
            writeSession_.tmpPath
        );
        auto released = releaseWriteSession_();
        if (!released) status = mapError(released.error());
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

void FileSystemRpcHandler::expireWriteSession_(uint32_t nowMs) {
    if (writeSession_.lease.valid() && !files_.owns(writeSession_.lease)) {
        (void)releaseWriteSession_();
        return;
    }
    if (!hasActiveWriteSession()) {
        return;
    }
    if (!writeSessionIdleExpired(nowMs)) {
        return;
    }
    abortWriteSession();
}

FLASHMEM void FileSystemRpcHandler::clearWriteSession_() {
    writeSession_ = WriteSession{};
}

FLASHMEM oc::type::Result<void> FileSystemRpcHandler::releaseWriteSession_() {
    if (!writeSession_.lease.valid()) {
        clearWriteSession_();
        return oc::type::Result<void>::ok();
    }
    auto released = files_.releaseMutation(writeSession_.lease);
    clearWriteSession_();
    return released;
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

}  // namespace core::protocol::filesystem

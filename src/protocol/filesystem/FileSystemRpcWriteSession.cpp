#include "protocol/filesystem/FileSystemRpcInternal.hpp"

#include <cstdio>
#include <cstring>

#include <config/PlatformCompat.hpp>

#include "persistence/AtomicProductFile.hpp"

namespace core::protocol::filesystem {

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

    if (internal::isConditionalMutationReservedPath(files_, path)) {
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
               offset > writeSession_.expectedSize ||
               static_cast<uint32_t>(size) > writeSession_.expectedSize - offset) {
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
            auto commit = core::persistence::commitProductFileTemp(
                files_,
                writeSession_.finalPath,
                writeSession_.backupPath,
                writeSession_.tmpPath
            );
            if (!commit) {
                status = mapError(commit.error());
            }
        }
    }

    if (status == FileSystemRpcStatus::OK) {
        clearWriteSession_();
    } else if (writeFinished) {
        (void)files_.remove(writeSession_.tmpPath);
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

void FileSystemRpcHandler::expireWriteSession_(uint32_t nowMs) {
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

}  // namespace core::protocol::filesystem

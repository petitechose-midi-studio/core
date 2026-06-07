#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/interface/ITransport.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductFileService.hpp"

namespace core::protocol::filesystem {

inline constexpr uint8_t FILESYSTEM_RPC_SCHEMA = 1;
inline constexpr uint8_t FILESYSTEM_RPC_ID_MIN = 0xE0;
inline constexpr uint8_t FILESYSTEM_RPC_ID_MAX = 0xEF;
inline constexpr size_t FILESYSTEM_RPC_MAX_CHUNK_SIZE = 512;
inline constexpr size_t FILESYSTEM_RPC_RESPONSE_BUFFER_SIZE = 1024;
inline constexpr uint8_t FILESYSTEM_RPC_MAX_LIST_ENTRIES = 8;
inline constexpr uint32_t FILESYSTEM_RPC_DEFAULT_WRITE_TIMEOUT_MS = 30'000;

enum class FileSystemRpcMessageId : uint8_t {
    STAT_REQUEST = 0xE0,
    STAT_RESPONSE = 0xE1,
    LIST_REQUEST = 0xE2,
    LIST_RESPONSE = 0xE3,
    READ_REQUEST = 0xE4,
    READ_RESPONSE = 0xE5,
    WRITE_BEGIN_REQUEST = 0xE6,
    WRITE_BEGIN_RESPONSE = 0xE7,
    WRITE_CHUNK_REQUEST = 0xE8,
    WRITE_CHUNK_RESPONSE = 0xE9,
    WRITE_COMMIT_REQUEST = 0xEA,
    WRITE_COMMIT_RESPONSE = 0xEB,
    WRITE_ABORT_REQUEST = 0xEC,
    WRITE_ABORT_RESPONSE = 0xED,
    ERROR_RESPONSE = 0xEF,
};

enum class FileSystemRpcStatus : uint8_t {
    OK = 0,
    INVALID_MESSAGE = 1,
    INVALID_ARGUMENT = 2,
    NOT_FOUND = 3,
    BUSY = 4,
    TOO_LARGE = 5,
    STORAGE_ERROR = 6,
    INVALID_STATE = 7,
    UNSUPPORTED = 8,
};

enum class FileSystemRpcFileType : uint8_t {
    MISSING = 0,
    FILE = 1,
    DIRECTORY = 2,
    OTHER = 3,
};

struct FileSystemRpcFrame {
    FileSystemRpcMessageId messageId = FileSystemRpcMessageId::ERROR_RESPONSE;
    uint8_t schema = 0;
    uint16_t requestId = 0;
    const uint8_t* payload = nullptr;
    size_t payloadSize = 0;
};

struct FileSystemRpcStatResponse {
    uint16_t requestId = 0;
    FileSystemRpcStatus status = FileSystemRpcStatus::INVALID_MESSAGE;
    FileSystemRpcFileType type = FileSystemRpcFileType::MISSING;
    uint32_t sizeBytes = 0;
};

struct FileSystemRpcListEntry {
    char name[oc::interface::FILESYSTEM_MAX_NAME_LENGTH] = {};
    FileSystemRpcFileType type = FileSystemRpcFileType::MISSING;
    uint32_t sizeBytes = 0;
    bool nameTruncated = false;
};

struct FileSystemRpcListResponse {
    uint16_t requestId = 0;
    FileSystemRpcStatus status = FileSystemRpcStatus::INVALID_MESSAGE;
    uint16_t startIndex = 0;
    uint8_t entryCount = 0;
    bool hasMore = false;
    FileSystemRpcListEntry entries[FILESYSTEM_RPC_MAX_LIST_ENTRIES] = {};
};

struct FileSystemRpcReadResponse {
    uint16_t requestId = 0;
    FileSystemRpcStatus status = FileSystemRpcStatus::INVALID_MESSAGE;
    uint32_t offset = 0;
    uint16_t bytesRead = 0;
    const uint8_t* data = nullptr;
};

struct FileSystemRpcWriteResponse {
    uint16_t requestId = 0;
    FileSystemRpcStatus status = FileSystemRpcStatus::INVALID_MESSAGE;
    uint16_t sessionId = 0;
    uint16_t bytesWritten = 0;
};

class FileSystemRpcCodec {
public:
    static bool isFileSystemMessageId(uint8_t messageId);
    static bool isFileSystemRequestId(uint8_t messageId);
    static const char* messageName(FileSystemRpcMessageId messageId);

    static oc::type::Result<FileSystemRpcFrame> decodeFrame(const uint8_t* data, size_t size);

    static size_t encodeStatRequest(uint16_t requestId,
                                    const char* path,
                                    uint8_t* out,
                                    size_t outSize);
    static size_t encodeListRequest(uint16_t requestId,
                                    const char* path,
                                    uint16_t startIndex,
                                    uint8_t maxEntries,
                                    uint8_t* out,
                                    size_t outSize);
    static size_t encodeReadRequest(uint16_t requestId,
                                    const char* path,
                                    uint32_t offset,
                                    uint16_t size,
                                    uint8_t* out,
                                    size_t outSize);
    static size_t encodeWriteBeginRequest(uint16_t requestId,
                                          uint16_t sessionId,
                                          const char* path,
                                          uint32_t expectedSize,
                                          uint8_t* out,
                                          size_t outSize);
    static size_t encodeWriteChunkRequest(uint16_t requestId,
                                          uint16_t sessionId,
                                          uint32_t offset,
                                          const uint8_t* data,
                                          uint16_t size,
                                          uint8_t* out,
                                          size_t outSize);
    static size_t encodeWriteCommitRequest(uint16_t requestId,
                                           uint16_t sessionId,
                                           uint8_t* out,
                                           size_t outSize);
    static size_t encodeWriteAbortRequest(uint16_t requestId,
                                          uint16_t sessionId,
                                          uint8_t* out,
                                          size_t outSize);

    static oc::type::Result<FileSystemRpcStatResponse> decodeStatResponse(const uint8_t* data,
                                                                          size_t size);
    static oc::type::Result<FileSystemRpcListResponse> decodeListResponse(const uint8_t* data,
                                                                          size_t size);
    static oc::type::Result<FileSystemRpcReadResponse> decodeReadResponse(const uint8_t* data,
                                                                          size_t size);
    static oc::type::Result<FileSystemRpcWriteResponse> decodeWriteResponse(const uint8_t* data,
                                                                            size_t size);
};

class FileSystemRpcHandler {
public:
    struct Config {
        constexpr explicit Config(
            uint32_t timeoutMs = FILESYSTEM_RPC_DEFAULT_WRITE_TIMEOUT_MS
        ) : writeSessionTimeoutMs(timeoutMs) {}

        uint32_t writeSessionTimeoutMs;
    };

    explicit FileSystemRpcHandler(core::persistence::ProductFileService& files);
    FileSystemRpcHandler(core::persistence::ProductFileService& files, Config config);

    oc::type::Result<size_t> handleFrame(const uint8_t* request,
                                         size_t requestSize,
                                         uint32_t nowMs,
                                         uint8_t* response,
                                         size_t responseSize);

    void update(uint32_t nowMs);
    bool hasActiveWriteSession() const;
    void abortWriteSession();

private:
    static constexpr size_t PATH_BUFFER_SIZE = oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1;

    struct WriteSession {
        bool active = false;
        uint16_t sessionId = 0;
        uint32_t expectedSize = 0;
        uint32_t writtenBytes = 0;
        uint32_t lastActivityMs = 0;
        char finalPath[PATH_BUFFER_SIZE] = {};
        char tmpPath[PATH_BUFFER_SIZE] = {};
        char backupPath[PATH_BUFFER_SIZE] = {};
    };

    oc::type::Result<size_t> handleStat_(const FileSystemRpcFrame& frame,
                                         uint8_t* response,
                                         size_t responseSize);
    oc::type::Result<size_t> handleList_(const FileSystemRpcFrame& frame,
                                         uint8_t* response,
                                         size_t responseSize);
    oc::type::Result<size_t> handleRead_(const FileSystemRpcFrame& frame,
                                         uint8_t* response,
                                         size_t responseSize);
    oc::type::Result<size_t> handleWriteBegin_(const FileSystemRpcFrame& frame,
                                               uint32_t nowMs,
                                               uint8_t* response,
                                               size_t responseSize);
    oc::type::Result<size_t> handleWriteChunk_(const FileSystemRpcFrame& frame,
                                               uint32_t nowMs,
                                               uint8_t* response,
                                               size_t responseSize);
    oc::type::Result<size_t> handleWriteCommit_(const FileSystemRpcFrame& frame,
                                                uint8_t* response,
                                                size_t responseSize);
    oc::type::Result<size_t> handleWriteAbort_(const FileSystemRpcFrame& frame,
                                               uint8_t* response,
                                               size_t responseSize);
    oc::type::Result<size_t> encodeError_(uint16_t requestId,
                                          FileSystemRpcStatus status,
                                          uint8_t* response,
                                          size_t responseSize) const;

    void expireWriteSession_(uint32_t nowMs);
    void clearWriteSession_();
    bool copySessionPath_(const char* path, uint16_t sessionId);

    core::persistence::ProductFileService& files_;
    Config config_;
    WriteSession writeSession_{};
};

class FileSystemRpcEndpoint {
public:
    using NowProvider = uint32_t (*)();

    FileSystemRpcEndpoint(oc::interface::ITransport& transport,
                          core::persistence::ProductFileService& files,
                          NowProvider nowProvider,
                          FileSystemRpcHandler::Config handlerConfig =
                              FileSystemRpcHandler::Config());
    ~FileSystemRpcEndpoint();

    FileSystemRpcEndpoint(const FileSystemRpcEndpoint&) = delete;
    FileSystemRpcEndpoint& operator=(const FileSystemRpcEndpoint&) = delete;
    FileSystemRpcEndpoint(FileSystemRpcEndpoint&&) = delete;
    FileSystemRpcEndpoint& operator=(FileSystemRpcEndpoint&&) = delete;

    void begin();
    void end();
    bool active() const;

private:
    void handleReceive_(const uint8_t* data, size_t size);

    oc::interface::ITransport& transport_;
    NowProvider nowProvider_ = nullptr;
    FileSystemRpcHandler handler_;
    uint8_t response_[FILESYSTEM_RPC_RESPONSE_BUFFER_SIZE] = {};
    bool active_ = false;
};

}  // namespace core::protocol::filesystem

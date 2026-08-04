#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/type/Result.hpp>

namespace core::protocol::filesystem {

inline constexpr uint8_t FILESYSTEM_JOB_RPC_SCHEMA = 1U;
inline constexpr uint8_t FILESYSTEM_JOB_RPC_REQUEST_ID = 0xFCU;
inline constexpr uint8_t FILESYSTEM_JOB_RPC_RESPONSE_ID = 0xFDU;
inline constexpr const char FILESYSTEM_JOB_RPC_REQUEST_NAME[] = "FsJobRequest";
inline constexpr const char FILESYSTEM_JOB_RPC_RESPONSE_NAME[] = "FsJobResponse";
inline constexpr size_t FILESYSTEM_JOB_RPC_REQUEST_HEADER_BYTES = 16U;
inline constexpr size_t FILESYSTEM_JOB_RPC_RESPONSE_HEADER_BYTES = 20U;
inline constexpr size_t FILESYSTEM_JOB_RPC_CAPABILITIES_BYTES = 24U;
inline constexpr size_t FILESYSTEM_JOB_RPC_MAX_INNER_REQUEST_BYTES = 32'512U;
inline constexpr size_t FILESYSTEM_JOB_RPC_MAX_INNER_RESPONSE_BYTES = 32'512U;
inline constexpr uint32_t FILESYSTEM_JOB_RPC_MAX_DEADLINE_MS = 10'000U;
inline constexpr uint32_t FILESYSTEM_JOB_RPC_TERMINAL_RETENTION_MS = 30'000U;
inline constexpr uint32_t FILESYSTEM_JOB_RPC_MAX_PROGRESS_PER_MILLE = 1'000U;
inline constexpr uint8_t FILESYSTEM_JOB_RPC_MAX_CONCURRENT = 2U;
inline constexpr uint8_t FILESYSTEM_JOB_RPC_VERSION = 1U;
inline constexpr uint32_t FILESYSTEM_JOB_RPC_RETRY_AFTER_MS = 5U;

inline constexpr uint32_t FILESYSTEM_JOB_RPC_FEATURE_START = 1U << 0U;
inline constexpr uint32_t FILESYSTEM_JOB_RPC_FEATURE_POLL = 1U << 1U;
inline constexpr uint32_t FILESYSTEM_JOB_RPC_FEATURE_CANCEL = 1U << 2U;
inline constexpr uint32_t FILESYSTEM_JOB_RPC_FEATURE_TERMINAL_RETENTION = 1U << 3U;
inline constexpr uint32_t FILESYSTEM_JOB_RPC_FEATURE_TYPED_ERRORS = 1U << 4U;
inline constexpr uint32_t FILESYSTEM_JOB_RPC_FEATURE_LEGACY_MAPPING = 1U << 5U;
inline constexpr uint32_t FILESYSTEM_JOB_RPC_FEATURES =
    FILESYSTEM_JOB_RPC_FEATURE_START | FILESYSTEM_JOB_RPC_FEATURE_POLL |
    FILESYSTEM_JOB_RPC_FEATURE_CANCEL | FILESYSTEM_JOB_RPC_FEATURE_TERMINAL_RETENTION |
    FILESYSTEM_JOB_RPC_FEATURE_TYPED_ERRORS | FILESYSTEM_JOB_RPC_FEATURE_LEGACY_MAPPING;

inline constexpr uint8_t FILESYSTEM_JOB_RPC_FLAG_DUPLICATE_START = 1U << 0U;
inline constexpr uint8_t FILESYSTEM_JOB_RPC_FLAG_LEGACY_MAPPED = 1U << 1U;
inline constexpr uint8_t FILESYSTEM_JOB_RPC_FLAG_TERMINAL_RETAINED = 1U << 2U;
inline constexpr uint8_t FILESYSTEM_JOB_RPC_FLAG_CANCEL_TOO_LATE = 1U << 3U;
inline constexpr uint8_t FILESYSTEM_JOB_RPC_RESPONSE_FLAGS =
    FILESYSTEM_JOB_RPC_FLAG_DUPLICATE_START | FILESYSTEM_JOB_RPC_FLAG_LEGACY_MAPPED |
    FILESYSTEM_JOB_RPC_FLAG_TERMINAL_RETAINED | FILESYSTEM_JOB_RPC_FLAG_CANCEL_TOO_LATE;

enum class FileSystemJobCommand : uint8_t {
    CAPABILITIES = 0,
    START = 1,
    POLL = 2,
    CANCEL = 3,
};

enum class FileSystemJobState : uint8_t {
    NONE = 0,
    ACCEPTED = 1,
    PENDING = 2,
    COMPLETED = 3,
    CANCEL_PENDING = 4,
    CANCELLED = 5,
    FAILED = 6,
    REJECTED = 7,
};

enum class FileSystemJobError : uint8_t {
    NONE = 0,
    INVALID_MESSAGE = 1,
    INVALID_ARGUMENT = 2,
    UNSUPPORTED = 3,
    NOT_FOUND = 4,
    BUSY_PLAYING = 5,
    RESOURCE_EXHAUSTED = 6,
    CONFLICT = 7,
    PRECONDITION_FAILED = 8,
    DEADLINE_EXCEEDED = 9,
    MEDIA_CHANGED = 10,
    STORAGE_UNAVAILABLE = 11,
    STORAGE_READ_FAILED = 12,
    STORAGE_WRITE_FAILED = 13,
    STORAGE_CORRUPT = 14,
    CANCELLED = 15,
    INTERNAL = 16,
    LEGACY_BUSY = 17,
    LEGACY_STORAGE_ERROR = 18,
};

struct FileSystemJobRequest {
    uint16_t requestId = 0U;
    FileSystemJobCommand command = FileSystemJobCommand::CAPABILITIES;
    uint8_t flags = 0U;
    uint16_t reserved = 0U;
    uint32_t clientNonce = 0U;
    uint32_t jobId = 0U;
    uint32_t totalDeadlineMs = 0U;
    const uint8_t* innerRequest = nullptr;
    size_t innerRequestSize = 0U;
};

struct FileSystemJobResponse {
    uint16_t requestId = 0U;
    FileSystemJobCommand command = FileSystemJobCommand::CAPABILITIES;
    FileSystemJobState state = FileSystemJobState::NONE;
    FileSystemJobError error = FileSystemJobError::NONE;
    uint8_t flags = 0U;
    uint32_t clientNonce = 0U;
    uint32_t jobId = 0U;
    uint32_t retryAfterMs = 0U;
    uint32_t progressPerMille = 0U;
    const uint8_t* body = nullptr;
    size_t bodySize = 0U;
};

class FileSystemJobRpcCodec {
public:
    static bool isJobRequestId(uint8_t messageId);
    static bool isSupportedStartRequest(const uint8_t* data, size_t size);
    static bool isCanonicalLegacyResponse(const uint8_t* data, size_t size);
    static bool terminalRetained(uint32_t nowMs, uint32_t terminalAtMs);

    /** Decode a trustworthy v1 envelope/header without applying command rules. */
    static oc::type::Result<FileSystemJobRequest> decodeRequestHeader(const uint8_t* data,
                                                                      size_t size);
    static oc::type::Result<FileSystemJobRequest> decodeRequest(const uint8_t* data, size_t size);
    static oc::type::Result<size_t> encodeResponse(const FileSystemJobResponse& response,
                                                   uint8_t* output, size_t outputSize);
    static oc::type::Result<FileSystemJobResponse> decodeResponse(const uint8_t* data, size_t size);
};

constexpr bool fileSystemJobStateTerminal(FileSystemJobState state) {
    return state == FileSystemJobState::COMPLETED || state == FileSystemJobState::CANCELLED ||
           state == FileSystemJobState::FAILED || state == FileSystemJobState::REJECTED;
}

}  // namespace core::protocol::filesystem

#pragma once

#include <cstddef>
#include <cstdint>

#include <oc/interface/IFileSystem.hpp>
#include <oc/interface/ITransport.hpp>
#include <oc/type/Result.hpp>

#include "persistence/ProductDirectoryCatalog.hpp"
#include "persistence/ProductFileService.hpp"
#include "protocol/filesystem/FileSystemJobRpc.hpp"

namespace core::persistence {
namespace conditional_mutation {
class ConditionalMutationPlan;
}
class ProductFileCommitPlan;
class ProductTreeCleanupPlan;
}

namespace core::protocol::filesystem {

inline constexpr uint8_t FILESYSTEM_RPC_SCHEMA = 1;
inline constexpr uint8_t FILESYSTEM_RPC_ID_MIN = 0xE0;
inline constexpr uint8_t FILESYSTEM_RPC_ID_MAX = 0xFB;
inline constexpr size_t FILESYSTEM_RPC_MAX_CHUNK_SIZE = 30720;
inline constexpr uint32_t FILESYSTEM_RPC_MAX_UPLOAD_SIZE = 524'288U;
inline constexpr uint32_t FILESYSTEM_RPC_TOTAL_WRITE_TIMEOUT_MS = 10'000U;
inline constexpr size_t FILESYSTEM_RPC_REQUEST_BUFFER_SIZE = 32512;
inline constexpr size_t FILESYSTEM_RPC_RESPONSE_BUFFER_SIZE = 32512;
inline constexpr uint8_t FILESYSTEM_RPC_MAX_LIST_ENTRIES = 8;
inline constexpr size_t FILESYSTEM_RPC_SHA256_SIZE = 32;
inline constexpr uint32_t FILESYSTEM_RPC_DEFAULT_WRITE_TIMEOUT_MS = 30'000;
inline constexpr uint32_t FILESYSTEM_RPC_FEATURE_CAPABILITIES = 1u << 0;
inline constexpr uint32_t FILESYSTEM_RPC_FEATURE_WRITE_SESSIONS = 1u << 1;
inline constexpr uint32_t FILESYSTEM_RPC_FEATURE_FILE_MANAGEMENT = 1u << 2;
// Schema 1 remains wire-compatible. Clients MUST discover this optional
// extension before sending message ids 0xF8..0xFB to older firmware.
inline constexpr uint32_t FILESYSTEM_RPC_FEATURE_CONDITIONAL_MUTATIONS = 1u << 3;
inline constexpr uint32_t FILESYSTEM_RPC_FEATURE_PERSISTENCE_JOBS = 1u << 4;
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
    MKDIR_REQUEST = 0xF0,
    MKDIR_RESPONSE = 0xF1,
    DELETE_REQUEST = 0xF2,
    DELETE_RESPONSE = 0xF3,
    RENAME_REQUEST = 0xF4,
    RENAME_RESPONSE = 0xF5,
    CAPABILITIES_REQUEST = 0xF6,
    CAPABILITIES_RESPONSE = 0xF7,
    CONDITIONAL_REPLACE_REQUEST = 0xF8,
    CONDITIONAL_REPLACE_RESPONSE = 0xF9,
    CONDITIONAL_DELETE_REQUEST = 0xFA,
    CONDITIONAL_DELETE_RESPONSE = 0xFB,
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
    PRECONDITION_FAILED = 9,
};

enum class FileSystemRpcConditionalRecoveryState : uint8_t {
    NOT_CHECKED = 0,
    READY,
    CORRUPT_JOURNAL_QUARANTINED,
    BLOCKED,
};

enum class FileSystemRpcMutationOutcome : uint8_t {
    NONE = 0,
    APPLIED = 1,
    ALREADY_APPLIED = 2,
};

enum class FileSystemRpcMutationSubject : uint8_t {
    NONE = 0,
    SOURCE = 1,
    STAGING = 2,
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

struct FileSystemRpcStatusResponse {
    uint16_t requestId = 0;
    FileSystemRpcMessageId messageId = FileSystemRpcMessageId::ERROR_RESPONSE;
    FileSystemRpcStatus status = FileSystemRpcStatus::INVALID_MESSAGE;
};

struct FileSystemRpcCapabilitiesResponse {
    uint16_t requestId = 0;
    FileSystemRpcStatus status = FileSystemRpcStatus::INVALID_MESSAGE;
    uint8_t rpcSchema = 0;
    uint16_t maxChunkSize = 0;
    uint16_t responseBufferSize = 0;
    uint8_t maxListEntries = 0;
    uint16_t maxPathLength = 0;
    uint32_t featureFlags = 0;
};

struct FileSystemRpcConditionalMutationResponse {
    uint16_t requestId = 0;
    FileSystemRpcMessageId messageId = FileSystemRpcMessageId::ERROR_RESPONSE;
    FileSystemRpcStatus status = FileSystemRpcStatus::INVALID_MESSAGE;
    FileSystemRpcMutationOutcome outcome = FileSystemRpcMutationOutcome::NONE;
    FileSystemRpcMutationSubject subject = FileSystemRpcMutationSubject::NONE;
    uint32_t operationId = 0;
    uint8_t observedSha256[FILESYSTEM_RPC_SHA256_SIZE] = {};
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
    static size_t encodeCapabilitiesRequest(uint16_t requestId,
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
    static size_t encodeMkdirRequest(uint16_t requestId,
                                     const char* path,
                                     uint8_t* out,
                                     size_t outSize);
    static size_t encodeDeleteRequest(uint16_t requestId,
                                      const char* path,
                                      bool recursive,
                                      uint8_t* out,
                                      size_t outSize);
    static size_t encodeRenameRequest(uint16_t requestId,
                                      const char* fromPath,
                                      const char* toPath,
                                      uint8_t* out,
                                      size_t outSize);
    static size_t encodeConditionalReplaceRequest(
        uint16_t requestId,
        uint32_t operationId,
        const char* currentPath,
        const char* stagingPath,
        const uint8_t expectedSourceSha256[FILESYSTEM_RPC_SHA256_SIZE],
        const uint8_t replacementSha256[FILESYSTEM_RPC_SHA256_SIZE],
        uint8_t* out,
        size_t outSize
    );
    static size_t encodeConditionalDeleteRequest(
        uint16_t requestId,
        uint32_t operationId,
        const char* path,
        const uint8_t expectedSourceSha256[FILESYSTEM_RPC_SHA256_SIZE],
        uint8_t* out,
        size_t outSize
    );

    static oc::type::Result<FileSystemRpcStatResponse> decodeStatResponse(const uint8_t* data,
                                                                          size_t size);
    static oc::type::Result<FileSystemRpcListResponse> decodeListResponse(const uint8_t* data,
                                                                          size_t size);
    static oc::type::Result<FileSystemRpcReadResponse> decodeReadResponse(const uint8_t* data,
                                                                          size_t size);
    static oc::type::Result<FileSystemRpcWriteResponse> decodeWriteResponse(const uint8_t* data,
                                                                            size_t size);
    static oc::type::Result<FileSystemRpcStatusResponse> decodeStatusResponse(const uint8_t* data,
                                                                              size_t size);
    static oc::type::Result<FileSystemRpcCapabilitiesResponse> decodeCapabilitiesResponse(
        const uint8_t* data,
        size_t size
    );
    static oc::type::Result<FileSystemRpcConditionalMutationResponse>
    decodeConditionalMutationResponse(const uint8_t* data, size_t size);
};

class FileSystemRpcHandler {
public:
    struct Config {
        constexpr explicit Config(
            uint32_t timeoutMs = FILESYSTEM_RPC_DEFAULT_WRITE_TIMEOUT_MS
        ) : writeSessionTimeoutMs(timeoutMs) {}

        uint32_t writeSessionTimeoutMs;
    };

    FileSystemRpcHandler(
        core::persistence::ProductFileService& files,
        core::persistence::ProductDirectoryCatalog& catalog
    );
    FileSystemRpcHandler(
        core::persistence::ProductFileService& files,
        core::persistence::ProductDirectoryCatalog& catalog,
        Config config
    );

    oc::type::Result<size_t> handleFrame(const uint8_t* request,
                                         size_t requestSize,
                                         uint32_t nowMs,
                                         uint8_t* response,
                                         size_t responseSize);

    /** Dispatch one frame whose timeout and storage-recovery gates are owned
     * by the foreground coordinator. This path performs no implicit
     * maintenance before the requested operation. */
    oc::type::Result<size_t> handleAdmittedFrame(const uint8_t* request,
                                                 size_t requestSize,
                                                 uint32_t nowMs,
                                                 uint8_t* response,
                                                 size_t responseSize,
                                                 core::persistence::ProductPersistenceWorkMeasurement*
                                                     measurement = nullptr);

    void update(uint32_t nowMs);
    bool hasActiveWriteSession() const;
    bool writeSessionIdleExpired(uint32_t nowMs) const;
    void abortWriteSession();
    oc::type::Result<size_t> encodeErrorResponse(
        uint16_t requestId,
        FileSystemRpcStatus status,
        uint8_t* response,
        size_t responseSize
    ) const;
    FileSystemRpcConditionalRecoveryState conditionalRecoveryState() const {
        return conditionalRecoveryState_;
    }
    FileSystemRpcStatus conditionalRecoveryStatus() const {
        return conditionalRecoveryStatus_;
    }

private:
    friend class FileSystemRpcEndpoint;

    static constexpr size_t PATH_BUFFER_SIZE = oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1;
    static constexpr size_t GENERATED_PATH_BUFFER_SIZE = 32;
    static constexpr uint32_t CONDITIONAL_RECOVERY_RETRY_MS = 500;

    struct WriteSession {
        uint32_t expectedSize = 0;
        uint32_t writtenBytes = 0;
        uint32_t lastActivityMs = 0;
        uint32_t payloadCrc32State = 0;
        core::persistence::ProductMutationLease lease{};
        uint16_t sessionId = 0;
        char finalPath[PATH_BUFFER_SIZE] = {};
        char tmpPath[GENERATED_PATH_BUFFER_SIZE] = {};
        char backupPath[GENERATED_PATH_BUFFER_SIZE] = {};
    };

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
    static_assert(sizeof(WriteSession) == 280U, "filesystem RPC write lease ABI drift");
#endif

    oc::type::Result<size_t> handleStat_(const FileSystemRpcFrame& frame,
                                         uint8_t* response,
                                         size_t responseSize);
    oc::type::Result<size_t> handleCapabilities_(const FileSystemRpcFrame& frame,
                                                 uint8_t* response,
                                                 size_t responseSize);
    oc::type::Result<size_t> handleList_(const FileSystemRpcFrame& frame,
                                         uint8_t* response,
                                         size_t responseSize,
                                         core::persistence::ProductPersistenceWorkMeasurement*
                                             measurement);
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
    oc::type::Result<size_t> beginCooperativeWriteCommit_(
        const FileSystemRpcFrame& frame,
        core::persistence::ProductFileCommitPlan& plan,
        uint16_t& sessionId,
        uint8_t* response,
        size_t responseSize
    );
    oc::type::Result<size_t> advanceCooperativeWriteCommit_(
        core::persistence::ProductFileCommitPlan& plan,
        uint16_t requestId,
        uint16_t sessionId,
        uint8_t* response,
        size_t responseSize
    );
    void cancelCooperativeWriteCommit_(
        core::persistence::ProductFileCommitPlan& plan
    );
    oc::type::Result<size_t> handleWriteAbort_(const FileSystemRpcFrame& frame,
                                               uint8_t* response,
                                               size_t responseSize);
    oc::type::Result<size_t> handleMkdir_(const FileSystemRpcFrame& frame,
                                          uint8_t* response,
                                          size_t responseSize);
    oc::type::Result<size_t> handleDelete_(const FileSystemRpcFrame& frame,
                                           uint8_t* response,
                                           size_t responseSize);
    oc::type::Result<size_t> handleRename_(const FileSystemRpcFrame& frame,
                                           uint8_t* response,
                                           size_t responseSize);
    oc::type::Result<size_t> handleConditionalReplace_(const FileSystemRpcFrame& frame,
                                                       uint32_t nowMs,
                                                       uint8_t* response,
                                                       size_t responseSize);
    oc::type::Result<size_t> handleConditionalDelete_(const FileSystemRpcFrame& frame,
                                                      uint32_t nowMs,
                                                      uint8_t* response,
                                                      size_t responseSize);
    oc::type::Result<size_t> beginCooperativeConditionalMutation_(
        const FileSystemRpcFrame& frame,
        core::persistence::conditional_mutation::ConditionalMutationPlan& plan,
        uint8_t* response,
        size_t responseSize
    );
    oc::type::Result<size_t> advanceCooperativeConditionalMutation_(
        core::persistence::conditional_mutation::ConditionalMutationPlan& plan,
        uint16_t requestId,
        uint32_t nowMs,
        uint8_t* response,
        size_t responseSize
    );
    void cancelCooperativeConditionalMutation_(
        core::persistence::conditional_mutation::ConditionalMutationPlan& plan
    );
    oc::type::Result<size_t> beginCooperativeRecursiveDelete_(
        const FileSystemRpcFrame& frame,
        core::persistence::ProductTreeCleanupPlan& plan,
        uint8_t* response,
        size_t responseSize
    );
    oc::type::Result<size_t> advanceCooperativeRecursiveDelete_(
        core::persistence::ProductTreeCleanupPlan& plan,
        uint16_t requestId,
        uint8_t* response,
        size_t responseSize,
        core::persistence::ProductPersistenceWorkMeasurement* measurement
    );
    void cancelCooperativeRecursiveDelete_(
        core::persistence::ProductTreeCleanupPlan& plan
    );
    oc::type::Result<size_t> encodeError_(uint16_t requestId,
                                          FileSystemRpcStatus status,
                                          uint8_t* response,
                                          size_t responseSize) const;

    void expireWriteSession_(uint32_t nowMs);
    void clearWriteSession_();
    oc::type::Result<void> releaseWriteSession_();
    bool copySessionPath_(const char* path, uint16_t sessionId);
    FileSystemRpcStatus recoverConditionalMutation_();
    bool conditionalRecoveryDue_(uint32_t nowMs) const;
    void updateConditionalRecovery_(uint32_t nowMs);

    core::persistence::ProductFileService& files_;
    core::persistence::ProductDirectoryCatalog& catalog_;
    Config config_;
    WriteSession writeSession_{};
    core::persistence::ProductStorageIdentity conditionalRecoveryIdentity_{};
    uint32_t conditionalRecoveryRetryAtMs_ = 0;
    FileSystemRpcConditionalRecoveryState conditionalRecoveryState_ =
        FileSystemRpcConditionalRecoveryState::NOT_CHECKED;
    FileSystemRpcStatus conditionalRecoveryStatus_ = FileSystemRpcStatus::OK;
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(FileSystemRpcHandler) == 308U, "filesystem RPC handler ABI drift");
static_assert(alignof(FileSystemRpcHandler) == 4U, "filesystem RPC handler alignment drift");
#endif

class FileSystemRpcEndpoint {
public:
    using NowProvider = uint32_t (*)();
    using MicrosProvider = uint32_t (*)();

    FileSystemRpcEndpoint(oc::interface::ITransport& transport,
                          core::persistence::ProductFileService& files,
                          core::persistence::ProductDirectoryCatalog& catalog,
                          NowProvider nowProvider,
                          FileSystemRpcHandler::Config handlerConfig =
                              FileSystemRpcHandler::Config(),
                          MicrosProvider microsProvider = nullptr);
    ~FileSystemRpcEndpoint();

    FileSystemRpcEndpoint(const FileSystemRpcEndpoint&) = delete;
    FileSystemRpcEndpoint& operator=(const FileSystemRpcEndpoint&) = delete;
    FileSystemRpcEndpoint(FileSystemRpcEndpoint&&) = delete;
    FileSystemRpcEndpoint& operator=(FileSystemRpcEndpoint&&) = delete;

    void begin();
    void end();
    void advance(uint32_t nowMs, bool playbackActive);
    bool active() const;

private:
    static constexpr uint8_t JOB_RECORD_NONE = 0xFFU;
    static constexpr uint8_t JOB_RECORD_COUNT = 32U;
    static constexpr size_t JOB_TERMINAL_RESPONSE_BYTES = 72U;

    static constexpr uint8_t JOB_FLAG_OCCUPIED = 1U << 0U;
    static constexpr uint8_t JOB_FLAG_CANCEL_REQUESTED = 1U << 1U;
    static constexpr uint8_t JOB_FLAG_CANCEL_TOO_LATE = 1U << 2U;
    static constexpr uint8_t JOB_FLAG_DEADLINE_REACHED = 1U << 3U;

    enum class PendingOperation : uint8_t {
        FRAME = 0,
        WRITE_COMMIT,
        CONDITIONAL_MUTATION,
        TREE_CLEANUP,
    };

    struct PendingFrame {
        alignas(8) uint8_t data[FILESYSTEM_RPC_REQUEST_BUFFER_SIZE] = {};
        size_t size = 0;
        core::persistence::ProductPersistenceJobToken token{};
        uint16_t requestId = 0;
        uint16_t sessionId = 0;
        PendingOperation operation = PendingOperation::FRAME;
        bool uploadContinuation = false;
        uint8_t jobRecordIndex = JOB_RECORD_NONE;
    };

    struct JobRecord {
        uint8_t terminalResponse[JOB_TERMINAL_RESPONSE_BYTES] = {};
        uint8_t requestDigest[FILESYSTEM_RPC_SHA256_SIZE] = {};
        uint32_t clientNonce = 0U;
        uint32_t jobId = 0U;
        uint32_t innerSize = 0U;
        uint32_t admittedAtMs = 0U;
        uint32_t deadlineMs = 0U;
        uint32_t terminalAtMs = 0U;
        uint32_t mediaGeneration = 0U;
        FileSystemJobState state = FileSystemJobState::NONE;
        FileSystemJobError error = FileSystemJobError::NONE;
        uint8_t responseSize = 0U;
        uint8_t flags = 0U;
    };

    static_assert(sizeof(JobRecord) <= 136U,
                  "filesystem job record exceeds compact PSRAM contract");

    void handleReceive_(const uint8_t* data, size_t size);
    void handleJobReceive_(const uint8_t* data, size_t size);
    void handleJobStart_(const FileSystemJobRequest& request, uint32_t nowMs);
    void sendJobResponse_(const FileSystemJobResponse& response);
    void sendJobRejected_(const FileSystemJobRequest& request, FileSystemJobError error,
                          uint32_t jobId = 0U);
    void sendJobRecordResponse_(const FileSystemJobRequest& request, const JobRecord& record,
                                uint8_t flags = 0U);
    void sendError_(const uint8_t* data, size_t size, FileSystemRpcStatus status);
    void sendErrorForRequest_(uint16_t requestId, FileSystemRpcStatus status);
    PendingFrame* emptyFrame_();
    PendingFrame* activeFrame_();
    PendingFrame* frameForJobRecord_(uint8_t recordIndex);
    JobRecord* freeJobRecord_();
    JobRecord* jobRecordForNonce_(uint32_t clientNonce);
    JobRecord* jobRecordForIdentity_(uint32_t clientNonce, uint32_t jobId);
    JobRecord* jobRecordForFrame_(const PendingFrame& frame);
    uint8_t jobRecordIndex_(const JobRecord& record) const;
    void resetJobRecord_(JobRecord& record);
    void reapExpiredJobRecords_(uint32_t nowMs);
    bool jobIrreversible_(const PendingFrame& frame) const;
    bool advanceJobInterruption_(PendingFrame& frame, JobRecord& record, uint32_t nowMs,
                                 bool playbackActive);
    bool prepareJobAdvance_(PendingFrame& frame, JobRecord& record, uint32_t nowMs,
                            bool playbackActive);
    void terminalizeJobResponse_(PendingFrame& frame, bool responseValid, size_t responseSize,
                                 uint32_t nowMs);
    void failStaleFrame_(PendingFrame& frame, uint32_t nowMs, FileSystemRpcStatus legacyStatus,
                         FileSystemJobError jobError);
    void terminalizeJob_(PendingFrame& frame, FileSystemJobState state, FileSystemJobError error,
                         uint32_t nowMs, const uint8_t* response = nullptr,
                         size_t responseSize = 0U);
    void clearFrame_(PendingFrame& frame);
    void cancelFrameOperation_(PendingFrame& frame);
    void cancelPendingJobs_();
    void advanceUploadTimeout_(uint32_t nowMs);
    bool uploadPromotionPending_() const;
    static bool isUploadContinuation_(FileSystemRpcMessageId messageId);
    static core::persistence::ProductPersistenceWorkQuota quotaFor_(
        const PendingFrame& frame,
        FileSystemRpcMessageId messageId
    );

    oc::interface::ITransport& transport_;
    core::persistence::ProductFileService& files_;
    NowProvider nowProvider_ = nullptr;
    MicrosProvider microsProvider_ = nullptr;
    FileSystemRpcHandler handler_;
    PendingFrame pending_[2]{};
    uint8_t response_[FILESYSTEM_RPC_RESPONSE_BUFFER_SIZE] = {};
    JobRecord job_records_[JOB_RECORD_COUNT]{};
    core::persistence::ProductPersistenceJobToken upload_job_{};
    uint32_t upload_started_ms_ = 0;
    bool active_ = false;
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(FileSystemRpcEndpoint) <= 106'496U,
              "filesystem RPC endpoint exceeds retained PSRAM ceiling");
#endif

}  // namespace core::protocol::filesystem

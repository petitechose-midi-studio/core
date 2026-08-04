#include "protocol/filesystem/FileSystemJobRpc.hpp"

#include <cstring>

#include <config/PlatformCompat.hpp>

#include "protocol/filesystem/FileSystemRpc.hpp"
#include "protocol/filesystem/FileSystemRpcInternal.hpp"

namespace core::protocol::filesystem {

namespace {

using internal::ByteReader;
using internal::ByteWriter;
using oc::type::ErrorCode;
using oc::type::Result;

const char kInvalidJobFrame[] PROGMEM = "invalid filesystem job frame";
const char kInvalidJobRequest[] PROGMEM = "invalid filesystem job request";
const char kInvalidJobResponse[] PROGMEM = "invalid filesystem job response";
const char kJobBufferTooSmall[] PROGMEM = "filesystem job buffer too small";

template <typename T>
FLASHMEM Result<T> invalid(const char* context) {
    return Result<T>::err({ErrorCode::INVALID_ARGUMENT, context});
}

template <typename T>
FLASHMEM Result<T> exhausted() {
    return Result<T>::err({ErrorCode::RESOURCE_EXHAUSTED, kJobBufferTooSmall});
}

FLASHMEM bool decodeCommand(uint8_t raw, FileSystemJobCommand& command) {
    if (raw > static_cast<uint8_t>(FileSystemJobCommand::CANCEL)) return false;
    command = static_cast<FileSystemJobCommand>(raw);
    return true;
}

FLASHMEM bool decodeState(uint8_t raw, FileSystemJobState& state) {
    if (raw > static_cast<uint8_t>(FileSystemJobState::REJECTED)) return false;
    state = static_cast<FileSystemJobState>(raw);
    return true;
}

FLASHMEM bool decodeError(uint8_t raw, FileSystemJobError& error) {
    if (raw > static_cast<uint8_t>(FileSystemJobError::LEGACY_STORAGE_ERROR)) { return false; }
    error = static_cast<FileSystemJobError>(raw);
    return true;
}

FLASHMEM bool writeEnvelope(ByteWriter& writer, uint8_t messageId, const char* name,
                            uint16_t requestId) {
    return writer.writeU8(messageId) && writer.writeString(name, UINT8_MAX) &&
           writer.writeU8(FILESYSTEM_JOB_RPC_SCHEMA) && writer.writeU16(requestId);
}

FLASHMEM bool readEnvelope(ByteReader& reader, uint8_t expectedMessageId, const char* expectedName,
                           uint16_t& requestId) {
    uint8_t messageId = 0U;
    uint8_t nameLength = 0U;
    const uint8_t* name = nullptr;
    uint8_t schema = 0U;
    const size_t expectedNameLength = std::strlen(expectedName);
    return reader.readU8(messageId) && messageId == expectedMessageId &&
           reader.readU8(nameLength) && nameLength == expectedNameLength &&
           reader.readBytes(name, nameLength) &&
           std::memcmp(name, expectedName, expectedNameLength) == 0 && reader.readU8(schema) &&
           schema == FILESYSTEM_JOB_RPC_SCHEMA && reader.readU16(requestId);
}

FLASHMEM bool isLegacyResponseId(uint8_t messageId) {
    switch (static_cast<FileSystemRpcMessageId>(messageId)) {
        case FileSystemRpcMessageId::STAT_RESPONSE:
        case FileSystemRpcMessageId::LIST_RESPONSE:
        case FileSystemRpcMessageId::READ_RESPONSE:
        case FileSystemRpcMessageId::WRITE_BEGIN_RESPONSE:
        case FileSystemRpcMessageId::WRITE_CHUNK_RESPONSE:
        case FileSystemRpcMessageId::WRITE_COMMIT_RESPONSE:
        case FileSystemRpcMessageId::WRITE_ABORT_RESPONSE:
        case FileSystemRpcMessageId::ERROR_RESPONSE:
        case FileSystemRpcMessageId::MKDIR_RESPONSE:
        case FileSystemRpcMessageId::DELETE_RESPONSE:
        case FileSystemRpcMessageId::RENAME_RESPONSE:
        case FileSystemRpcMessageId::CAPABILITIES_RESPONSE:
        case FileSystemRpcMessageId::CONDITIONAL_REPLACE_RESPONSE:
        case FileSystemRpcMessageId::CONDITIONAL_DELETE_RESPONSE: return true;
        default: return false;
    }
}

FLASHMEM bool canonicalLegacyFrame(const uint8_t* data, size_t size, bool request) {
    if (!data || size < 5U) return false;
    const uint8_t messageId = data[0];
    if (request) {
        if (!FileSystemRpcCodec::isFileSystemRequestId(messageId)) return false;
    } else if (!isLegacyResponseId(messageId)) {
        return false;
    }

    const char* expectedName =
        FileSystemRpcCodec::messageName(static_cast<FileSystemRpcMessageId>(messageId));
    const size_t expectedNameLength = std::strlen(expectedName);
    if (data[1] != expectedNameLength) return false;
    const size_t schemaOffset = 2U + expectedNameLength;
    const size_t headerSize = schemaOffset + 3U;
    return headerSize <= size && std::memcmp(data + 2U, expectedName, expectedNameLength) == 0 &&
           data[schemaOffset] == FILESYSTEM_RPC_SCHEMA;
}

FLASHMEM bool responseSemanticsValid(const FileSystemJobResponse& response,
                                     bool decodedCapabilitiesBody) {
    if ((response.flags & ~FILESYSTEM_JOB_RPC_RESPONSE_FLAGS) != 0U ||
        response.progressPerMille > FILESYSTEM_JOB_RPC_MAX_PROGRESS_PER_MILLE) {
        return false;
    }

    if (response.command == FileSystemJobCommand::CAPABILITIES) {
        if (response.state != FileSystemJobState::NONE ||
            response.error != FileSystemJobError::NONE || response.flags != 0U ||
            response.clientNonce != 0U || response.jobId != 0U || response.retryAfterMs != 0U ||
            response.progressPerMille != 0U) {
            return false;
        }
        return decodedCapabilitiesBody
                   ? response.body != nullptr &&
                         response.bodySize == FILESYSTEM_JOB_RPC_CAPABILITIES_BYTES
                   : response.bodySize == 0U;
    }

    if (response.clientNonce == 0U) return false;
    if (response.command != FileSystemJobCommand::START && response.jobId == 0U) { return false; }
    if (response.command == FileSystemJobCommand::START && response.jobId == 0U &&
        (response.state != FileSystemJobState::REJECTED ||
         response.error == FileSystemJobError::CONFLICT)) {
        return false;
    }

    switch (response.state) {
        case FileSystemJobState::ACCEPTED:
        case FileSystemJobState::PENDING:
        case FileSystemJobState::CANCEL_PENDING:
        case FileSystemJobState::COMPLETED:
            if (response.error != FileSystemJobError::NONE) return false;
            break;
        case FileSystemJobState::CANCELLED:
            if (response.error != FileSystemJobError::CANCELLED) return false;
            break;
        case FileSystemJobState::FAILED:
        case FileSystemJobState::REJECTED:
            if (response.error == FileSystemJobError::NONE) return false;
            break;
        case FileSystemJobState::NONE:
        default: return false;
    }

    const bool pending = response.state == FileSystemJobState::ACCEPTED ||
                         response.state == FileSystemJobState::PENDING ||
                         response.state == FileSystemJobState::CANCEL_PENDING;
    if ((pending && response.retryAfterMs > FILESYSTEM_JOB_RPC_MAX_DEADLINE_MS) ||
        (!pending && response.retryAfterMs != 0U)) {
        return false;
    }

    if (response.state == FileSystemJobState::COMPLETED) {
        if (response.bodySize > FILESYSTEM_JOB_RPC_MAX_INNER_RESPONSE_BYTES ||
            !canonicalLegacyFrame(response.body, response.bodySize, false)) {
            return false;
        }
    } else if (response.bodySize != 0U) {
        return false;
    }

    if ((response.flags & FILESYSTEM_JOB_RPC_FLAG_DUPLICATE_START) != 0U &&
        (response.command != FileSystemJobCommand::START || response.jobId == 0U ||
         response.state == FileSystemJobState::REJECTED)) {
        return false;
    }
    if ((response.flags & FILESYSTEM_JOB_RPC_FLAG_TERMINAL_RETAINED) != 0U &&
        (response.command != FileSystemJobCommand::POLL ||
         !fileSystemJobStateTerminal(response.state))) {
        return false;
    }
    if ((response.flags & FILESYSTEM_JOB_RPC_FLAG_CANCEL_TOO_LATE) != 0U &&
        (response.command != FileSystemJobCommand::CANCEL ||
         (response.state != FileSystemJobState::PENDING &&
          response.state != FileSystemJobState::COMPLETED &&
          response.state != FileSystemJobState::FAILED))) {
        return false;
    }
    if ((response.error == FileSystemJobError::LEGACY_BUSY ||
         response.error == FileSystemJobError::LEGACY_STORAGE_ERROR) &&
        (response.flags & FILESYSTEM_JOB_RPC_FLAG_LEGACY_MAPPED) == 0U) {
        return false;
    }
    if ((response.state == FileSystemJobState::NONE ||
         response.state == FileSystemJobState::REJECTED) &&
        response.progressPerMille != 0U) {
        return false;
    }
    return true;
}

FLASHMEM bool writeCapabilities(ByteWriter& writer) {
    return writer.writeU8(FILESYSTEM_JOB_RPC_VERSION) &&
           writer.writeU8(FILESYSTEM_JOB_RPC_MAX_CONCURRENT) && writer.writeU16(0U) &&
           writer.writeU32(FILESYSTEM_JOB_RPC_FEATURES) &&
           writer.writeU32(FILESYSTEM_JOB_RPC_MAX_INNER_REQUEST_BYTES) &&
           writer.writeU32(FILESYSTEM_JOB_RPC_MAX_INNER_RESPONSE_BYTES) &&
           writer.writeU32(FILESYSTEM_JOB_RPC_MAX_DEADLINE_MS) &&
           writer.writeU32(FILESYSTEM_JOB_RPC_TERMINAL_RETENTION_MS);
}

FLASHMEM bool capabilitiesValid(const uint8_t* data, size_t size) {
    if (!data || size != FILESYSTEM_JOB_RPC_CAPABILITIES_BYTES) return false;
    ByteReader reader(data, size);
    uint8_t version = 0U;
    uint8_t maxConcurrent = 0U;
    uint16_t reserved = 0U;
    uint32_t features = 0U;
    uint32_t maxRequest = 0U;
    uint32_t maxResponse = 0U;
    uint32_t maxDeadline = 0U;
    uint32_t retention = 0U;
    return reader.readU8(version) && version == FILESYSTEM_JOB_RPC_VERSION &&
           reader.readU8(maxConcurrent) && maxConcurrent == FILESYSTEM_JOB_RPC_MAX_CONCURRENT &&
           reader.readU16(reserved) && reserved == 0U && reader.readU32(features) &&
           features == FILESYSTEM_JOB_RPC_FEATURES && reader.readU32(maxRequest) &&
           maxRequest == FILESYSTEM_JOB_RPC_MAX_INNER_REQUEST_BYTES &&
           reader.readU32(maxResponse) &&
           maxResponse == FILESYSTEM_JOB_RPC_MAX_INNER_RESPONSE_BYTES &&
           reader.readU32(maxDeadline) && maxDeadline == FILESYSTEM_JOB_RPC_MAX_DEADLINE_MS &&
           reader.readU32(retention) && retention == FILESYSTEM_JOB_RPC_TERMINAL_RETENTION_MS &&
           reader.remaining() == 0U;
}

}  // namespace

FLASHMEM bool FileSystemJobRpcCodec::isJobRequestId(uint8_t messageId) {
    return messageId == FILESYSTEM_JOB_RPC_REQUEST_ID;
}

FLASHMEM bool FileSystemJobRpcCodec::isSupportedStartRequest(const uint8_t* data, size_t size) {
    if (!canonicalLegacyFrame(data, size, true)) return false;
    switch (static_cast<FileSystemRpcMessageId>(data[0])) {
        case FileSystemRpcMessageId::WRITE_COMMIT_REQUEST:
        case FileSystemRpcMessageId::MKDIR_REQUEST:
        case FileSystemRpcMessageId::DELETE_REQUEST:
        case FileSystemRpcMessageId::RENAME_REQUEST:
        case FileSystemRpcMessageId::CONDITIONAL_REPLACE_REQUEST:
        case FileSystemRpcMessageId::CONDITIONAL_DELETE_REQUEST: return true;
        default: return false;
    }
}

FLASHMEM bool FileSystemJobRpcCodec::isCanonicalLegacyResponse(const uint8_t* data, size_t size) {
    return canonicalLegacyFrame(data, size, false);
}

FLASHMEM bool FileSystemJobRpcCodec::terminalRetained(uint32_t nowMs, uint32_t terminalAtMs) {
    return static_cast<uint32_t>(nowMs - terminalAtMs) <= FILESYSTEM_JOB_RPC_TERMINAL_RETENTION_MS;
}

FLASHMEM Result<FileSystemJobRequest> FileSystemJobRpcCodec::decodeRequest(const uint8_t* data,
                                                                           size_t size) {
    auto decoded = decodeRequestHeader(data, size);
    if (!decoded) return decoded;
    FileSystemJobRequest request = decoded.value();
    if (request.flags != 0U || request.reserved != 0U) {
        return invalid<FileSystemJobRequest>(kInvalidJobRequest);
    }

    switch (request.command) {
        case FileSystemJobCommand::CAPABILITIES:
            if (request.clientNonce != 0U || request.jobId != 0U || request.totalDeadlineMs != 0U ||
                request.innerRequestSize != 0U) {
                return invalid<FileSystemJobRequest>(kInvalidJobRequest);
            }
            break;
        case FileSystemJobCommand::START:
            if (request.clientNonce == 0U || request.jobId != 0U || request.totalDeadlineMs == 0U ||
                request.totalDeadlineMs > FILESYSTEM_JOB_RPC_MAX_DEADLINE_MS ||
                request.innerRequestSize > FILESYSTEM_JOB_RPC_MAX_INNER_REQUEST_BYTES ||
                !canonicalLegacyFrame(request.innerRequest, request.innerRequestSize, true)) {
                return invalid<FileSystemJobRequest>(kInvalidJobRequest);
            }
            break;
        case FileSystemJobCommand::POLL:
        case FileSystemJobCommand::CANCEL:
            if (request.clientNonce == 0U || request.jobId == 0U || request.totalDeadlineMs != 0U ||
                request.innerRequestSize != 0U) {
                return invalid<FileSystemJobRequest>(kInvalidJobRequest);
            }
            break;
    }
    return Result<FileSystemJobRequest>::ok(request);
}

FLASHMEM Result<FileSystemJobRequest> FileSystemJobRpcCodec::decodeRequestHeader(
    const uint8_t* data, size_t size) {
    if (!data) return invalid<FileSystemJobRequest>(kInvalidJobFrame);
    ByteReader reader(data, size);
    FileSystemJobRequest request{};
    if (!readEnvelope(reader, FILESYSTEM_JOB_RPC_REQUEST_ID, FILESYSTEM_JOB_RPC_REQUEST_NAME,
                      request.requestId)) {
        return invalid<FileSystemJobRequest>(kInvalidJobFrame);
    }

    uint8_t rawCommand = 0U;
    if (!reader.readU8(rawCommand) || !decodeCommand(rawCommand, request.command) ||
        !reader.readU8(request.flags) || !reader.readU16(request.reserved) ||
        !reader.readU32(request.clientNonce) || !reader.readU32(request.jobId) ||
        !reader.readU32(request.totalDeadlineMs)) {
        return invalid<FileSystemJobRequest>(kInvalidJobRequest);
    }
    request.innerRequest = reader.current();
    request.innerRequestSize = reader.remaining();

    return Result<FileSystemJobRequest>::ok(request);
}

FLASHMEM Result<size_t> FileSystemJobRpcCodec::encodeResponse(const FileSystemJobResponse& response,
                                                              uint8_t* output, size_t outputSize) {
    if (!output || !responseSemanticsValid(response, false)) {
        return invalid<size_t>(kInvalidJobResponse);
    }

    ByteWriter writer(output, outputSize);
    if (!writeEnvelope(writer, FILESYSTEM_JOB_RPC_RESPONSE_ID, FILESYSTEM_JOB_RPC_RESPONSE_NAME,
                       response.requestId) ||
        !writer.writeU8(static_cast<uint8_t>(response.command)) ||
        !writer.writeU8(static_cast<uint8_t>(response.state)) ||
        !writer.writeU8(static_cast<uint8_t>(response.error)) || !writer.writeU8(response.flags) ||
        !writer.writeU32(response.clientNonce) || !writer.writeU32(response.jobId) ||
        !writer.writeU32(response.retryAfterMs) || !writer.writeU32(response.progressPerMille)) {
        return exhausted<size_t>();
    }
    if (response.command == FileSystemJobCommand::CAPABILITIES) {
        if (!writeCapabilities(writer)) return exhausted<size_t>();
    } else if (response.bodySize != 0U && !writer.writeBytes(response.body, response.bodySize)) {
        return exhausted<size_t>();
    }
    return Result<size_t>::ok(writer.position());
}

FLASHMEM Result<FileSystemJobResponse> FileSystemJobRpcCodec::decodeResponse(const uint8_t* data,
                                                                             size_t size) {
    if (!data) return invalid<FileSystemJobResponse>(kInvalidJobFrame);
    ByteReader reader(data, size);
    FileSystemJobResponse response{};
    if (!readEnvelope(reader, FILESYSTEM_JOB_RPC_RESPONSE_ID, FILESYSTEM_JOB_RPC_RESPONSE_NAME,
                      response.requestId)) {
        return invalid<FileSystemJobResponse>(kInvalidJobFrame);
    }

    uint8_t rawCommand = 0U;
    uint8_t rawState = 0U;
    uint8_t rawError = 0U;
    if (!reader.readU8(rawCommand) || !decodeCommand(rawCommand, response.command) ||
        !reader.readU8(rawState) || !decodeState(rawState, response.state) ||
        !reader.readU8(rawError) || !decodeError(rawError, response.error) ||
        !reader.readU8(response.flags) || !reader.readU32(response.clientNonce) ||
        !reader.readU32(response.jobId) || !reader.readU32(response.retryAfterMs) ||
        !reader.readU32(response.progressPerMille)) {
        return invalid<FileSystemJobResponse>(kInvalidJobResponse);
    }
    response.body = reader.current();
    response.bodySize = reader.remaining();
    const bool capabilities = response.command == FileSystemJobCommand::CAPABILITIES;
    if (!responseSemanticsValid(response, capabilities) ||
        (capabilities && !capabilitiesValid(response.body, response.bodySize))) {
        return invalid<FileSystemJobResponse>(kInvalidJobResponse);
    }
    return Result<FileSystemJobResponse>::ok(response);
}

}  // namespace core::protocol::filesystem

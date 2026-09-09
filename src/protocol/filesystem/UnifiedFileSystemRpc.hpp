#pragma once

#include <cstddef>
#include <cstdint>

namespace core::protocol::filesystem::unified {

inline constexpr uint8_t REQUEST = 0xfc, RESPONSE = 0xfd, VERSION = 2;
inline constexpr size_t HEADER = 24, MAX_BODY = 32'512;
inline constexpr uint32_t MAX_DEADLINE_MS = 10'000;

enum class Operation : uint8_t {
    Capabilities, Stat, List, Read, UploadBegin, UploadChunk, UploadCommit,
    UploadAbort, Mkdir, Delete, Rename, ConditionalReplace, ConditionalDelete, Poll, Cancel,
};
enum class State : uint8_t { Request, Complete, Pending, Failed, Cancelled };
enum class Error : uint16_t {
    None, InvalidMessage, InvalidArgument, Unsupported, NotFound, BusyPlaying,
    ResourceExhausted, Conflict, PreconditionFailed, DeadlineExceeded, MediaChanged,
    StorageUnavailable, StorageReadFailed, StorageWriteFailed, StorageCorrupt,
    Cancelled, Internal, ResultExpired, CancelTooLate,
};

struct Frame {
    Operation operation = Operation::Capabilities;
    State state = State::Request;
    uint16_t requestId = 0;
    Error error = Error::None;
    uint32_t nonce = 0;
    uint32_t operationId = 0;
    uint32_t delayMs = 0;
    const uint8_t* body = nullptr;
    size_t bodySize = 0;
    bool replayed = false;
};

bool retained(Operation operation);
bool valid(const Frame& frame);
// Decoding borrows input; failure leaves the output frame unchanged.
bool decode(const uint8_t* data, size_t size, Frame& out);
// Encoding validates completely before writing. Input/output must not overlap.
size_t encode(const Frame& frame, uint8_t* out, size_t capacity);

}  // namespace core::protocol::filesystem::unified

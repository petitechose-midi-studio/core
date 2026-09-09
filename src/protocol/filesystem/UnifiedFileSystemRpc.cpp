#include "UnifiedFileSystemRpc.hpp"

#include <cstring>
#include <config/PlatformCompat.hpp>

namespace core::protocol::filesystem::unified {

FLASHMEM bool retained(Operation op) {
    return op == Operation::UploadCommit || op == Operation::Mkdir || op == Operation::Delete
        || op == Operation::Rename || op == Operation::ConditionalReplace || op == Operation::ConditionalDelete;
}

FLASHMEM bool valid(const Frame& f) {
    if (f.requestId == 0 || f.bodySize > MAX_BODY || (f.bodySize != 0 && !f.body)
        || static_cast<uint8_t>(f.operation) > static_cast<uint8_t>(Operation::Cancel)
        || static_cast<uint8_t>(f.state) > static_cast<uint8_t>(State::Cancelled)
        || static_cast<uint16_t>(f.error) > static_cast<uint16_t>(Error::TooLarge)) return false;
    if (f.replayed && (f.state == State::Request || !retained(f.operation) || f.operationId == 0)) return false;
    if (f.state == State::Request) {
        if (f.error != Error::None) return false;
        if (f.operation == Operation::Poll || f.operation == Operation::Cancel)
            return f.nonce != 0 && f.operationId != 0 && f.delayMs == 0 && f.bodySize == 0;
        if (retained(f.operation))
            return f.nonce != 0 && f.operationId == 0 && f.delayMs > 0 && f.delayMs <= MAX_DEADLINE_MS;
        return f.nonce == 0 && f.operationId == 0 && f.delayMs == 0;
    }
    if (retained(f.operation) || f.operation == Operation::Poll || f.operation == Operation::Cancel) {
        if (f.nonce == 0 || (f.operationId == 0 && f.state != State::Failed)) return false;
    } else if (f.nonce != 0 || f.operationId != 0 || f.state == State::Pending || f.state == State::Cancelled) {
        return false;
    }
    switch (f.state) {
        case State::Complete: return f.error == Error::None && f.delayMs == 0;
        case State::Pending: return f.error == Error::None && f.nonce != 0 && f.operationId != 0
            && f.delayMs > 0 && f.delayMs <= MAX_DEADLINE_MS && f.bodySize == 0;
        case State::Failed: {
            const bool details = f.operationId != 0 && f.bodySize == 35
                && (f.operation == Operation::ConditionalReplace || f.operation == Operation::ConditionalDelete
                    || f.operation == Operation::Poll || f.operation == Operation::Cancel)
                && f.body[0] <= 2 && f.body[1] <= 2 && f.body[2] <= 1;
            if (details && !f.body[2]) for (size_t i = 3; i < 35; ++i) if (f.body[i]) return false;
            return f.error != Error::None && f.error != Error::Cancelled
                && f.delayMs == 0 && (f.bodySize == 0 || details);
        }
        case State::Cancelled: return f.error == Error::Cancelled && f.nonce != 0
            && f.operationId != 0 && f.delayMs == 0 && f.bodySize == 0;
        default: return false;
    }
}

FLASHMEM bool decode(const uint8_t* data, size_t size, Frame& out) {
    if (!data || size < HEADER || data[1] != VERSION || data[6] || data[7]) return false;
    const auto u16 = [data](size_t i) { return static_cast<uint16_t>(data[i] | (uint16_t(data[i+1]) << 8)); };
    const auto u32 = [data](size_t i) { return uint32_t(data[i]) | (uint32_t(data[i+1]) << 8)
        | (uint32_t(data[i+2]) << 16) | (uint32_t(data[i+3]) << 24); };
    Frame frame{static_cast<Operation>(data[2]), static_cast<State>(data[3] & 0x7f), uint64_t(u32(24)) | uint64_t(u32(28)) << 32,
        static_cast<Error>(u16(4)), u32(8), u32(12), u32(16), data + HEADER, u32(20), (data[3] & 0x80) != 0};
    if (data[0] != (frame.state == State::Request ? REQUEST : RESPONSE)
        || frame.bodySize != size - HEADER || !valid(frame)) return false;
    out = frame;
    return true;
}

FLASHMEM size_t encode(const Frame& frame, uint8_t* out, size_t capacity) {
    if (!out || !valid(frame) || capacity < HEADER + frame.bodySize) return 0;
    out[0] = frame.state == State::Request ? REQUEST : RESPONSE;
    out[1] = VERSION; out[2] = static_cast<uint8_t>(frame.operation);
    out[3] = static_cast<uint8_t>(frame.state) | (frame.replayed ? 0x80 : 0);
    const auto put16 = [out](size_t i, uint16_t n) { out[i] = uint8_t(n); out[i+1] = uint8_t(n >> 8); };
    const auto put32 = [out](size_t i, uint32_t n) { for (size_t b = 0; b < 4; ++b) out[i+b] = uint8_t(n >> (8*b)); };
    put16(4, static_cast<uint16_t>(frame.error)); put16(6, 0);
    put32(24, uint32_t(frame.requestId)); put32(28, uint32_t(frame.requestId >> 32));
    put32(8, frame.nonce); put32(12, frame.operationId); put32(16, frame.delayMs);
    put32(20, static_cast<uint32_t>(frame.bodySize));
    if (frame.bodySize) std::memcpy(out + HEADER, frame.body, frame.bodySize);
    return HEADER + frame.bodySize;
}

}  // namespace core::protocol::filesystem::unified

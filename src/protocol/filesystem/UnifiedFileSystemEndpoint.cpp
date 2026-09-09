#include "UnifiedFileSystemEndpoint.hpp"
#include <config/PlatformCompat.hpp>
#include <cstring>

namespace core::protocol::filesystem::unified {

FLASHMEM Endpoint::~Endpoint() { end(); }

FLASHMEM void Endpoint::begin() {
    active_ = true;
    transport_.setOnReceive([this](const uint8_t* data, size_t size) { receive(data, size); });
}

FLASHMEM void Endpoint::end() {
    if (!active_) return;
    transport_.setOnReceive({});
    active_ = false;
    head_ = count_ = 0;
    largeOccupied_ = false;
}

FLASHMEM void Endpoint::reject(Frame request, Error error) {
    request.state = State::Failed; request.error = error; request.delayMs = 0;
    request.body = nullptr; request.bodySize = 0; request.replayed = false;
    uint8_t reply[HEADER];
    const auto size = encode(request, reply, sizeof(reply));
    if (size) transport_.send(reply, size);
}

FLASHMEM void Endpoint::receive(const uint8_t* data, size_t size) {
    if (!active_ || !data || size < HEADER || data[0] != REQUEST) return;
    Frame request;
    if (!decode(data, size, request) || request.state != State::Request) {
        // An intact header of another unified version can be rejected explicitly
        // without interpreting its body or admitting any storage operation.
        if (data[2] <= uint8_t(Operation::Cancel) && data[3] == 0) {
            const auto u32 = [data](size_t i) { return uint32_t(data[i]) | uint32_t(data[i+1]) << 8
                | uint32_t(data[i+2]) << 16 | uint32_t(data[i+3]) << 24; };
            request.operation = Operation(data[2]); request.requestId = uint64_t(u32(24)) | uint64_t(u32(28)) << 32;
            request.lifetime = uint64_t(u32(32)) | uint64_t(u32(36)) << 32;
            request.nonce = u32(8); request.operationId = u32(12);
            reject(request, data[1] != VERSION ? Error::Unsupported : Error::InvalidMessage);
        }
        return;
    }
    const bool large = size > SMALL_FRAME;
    if (large && request.operation != Operation::UploadChunk) { reject(request, Error::InvalidArgument); return; }
    if (count_ == QUEUE_CAPACITY || (large && largeOccupied_)) { reject(request, Error::ResourceExhausted); return; }
    auto& pending = queue_[(head_ + count_) % QUEUE_CAPACITY];
    std::memcpy(large ? large_ : pending.data, data, size);
    pending.size = uint16_t(size); pending.received = now_ ? now_() : 0;
    pending.media = files_.storageIdentity().mediaGeneration;
    largeOccupied_ = largeOccupied_ || large;
    ++count_;
}

FLASHMEM void Endpoint::advance(uint32_t nowMs, bool playing) {
    if (!active_) return;
    if (count_) {
        auto& pending = queue_[head_];
        const bool large = pending.size > SMALL_FRAME;
        const auto* data = large ? large_ : pending.data;
        Frame request;
        bool consumed = true;
        if (decode(data, pending.size, request)) {
            const auto deadline = retained(request.operation) ? request.delayMs : MAX_DEADLINE_MS;
            if (request.operation != Operation::Capabilities && pending.media != files_.storageIdentity().mediaGeneration)
                reject(request, Error::MediaChanged);
            else if (uint32_t(nowMs - pending.received) >= deadline) reject(request, Error::DeadlineExceeded);
            else {
                const auto size = service_.process(data, pending.size, nowMs, playing, response_, sizeof(response_), true);
                if (size) transport_.send(response_, size);
                else consumed = false; // Admission deferred; retain the original deadline and bytes.
            }
        }
        if (consumed) {
            if (large) largeOccupied_ = false;
            head_ = (head_ + 1) % QUEUE_CAPACITY;
            --count_;
        }
    }
    service_.advance(nowMs, playing, response_, sizeof(response_));
}

} // namespace core::protocol::filesystem::unified

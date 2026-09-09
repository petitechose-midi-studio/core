#pragma once

#include <oc/interface/ITransport.hpp>
#include "UnifiedFileTransfer.hpp"

namespace core::protocol::filesystem::unified {

// Transport callbacks only validate/copy bounded frames. Storage work stays in
// advance(), under the shared foreground persistence coordinator.
class Endpoint {
public:
    using NowProvider = uint32_t (*)();
    static constexpr size_t QUEUE_CAPACITY = 8;
    static constexpr size_t SMALL_FRAME = HEADER + 450;
    Endpoint(oc::interface::ITransport& transport,
             core::persistence::ProductFileService& files,
             core::persistence::ProductDirectoryCatalog& catalog,
             NowProvider now, NowProvider micros = nullptr)
        : transport_(transport), files_(files), service_(files, catalog, micros), now_(now) {}
    ~Endpoint();
    void begin();
    void end();
    void advance(uint32_t nowMs, bool playing);
private:
    void receive(const uint8_t* data, size_t size);
    void reject(Frame request, Error error);
    struct Pending {
        uint8_t data[SMALL_FRAME]{};
        uint16_t size = 0;
        uint32_t received = 0;
        uint32_t media = 0;
    };
    oc::interface::ITransport& transport_;
    core::persistence::ProductFileService& files_;
    FileTransfer service_;
    NowProvider now_;
    Pending queue_[QUEUE_CAPACITY]{};
    uint8_t large_[HEADER + MAX_BODY]{};
    uint8_t response_[HEADER + MAX_BODY]{};
    uint8_t head_ = 0, count_ = 0;
    bool largeOccupied_ = false, active_ = false;
};

} // namespace core::protocol::filesystem::unified

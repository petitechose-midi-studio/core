#pragma once

#include <cstdint>

namespace core::state {
struct CoreState;
}

namespace core::persistence {

class ProductFileService;

class ProjectSessionAutosaveService {
public:
    enum class Status : uint8_t {
        IDLE = 0,
        WAITING,
        BLOCKED,
        SAVED,
        CAPTURE_FAILED,
        SAVE_FAILED,
    };

    struct Result {
        Status status = Status::IDLE;
        uint32_t bytes = 0;
        uint32_t modifiedCounter = 0;

        bool saved() const {
            return status == Status::SAVED;
        }
    };

    explicit ProjectSessionAutosaveService(ProductFileService& files,
                                           uint32_t delayMs = 0);

    Result update(core::state::CoreState& state, uint32_t nowMs, bool writeBlocked = false);
    Result flush(core::state::CoreState& state);

private:
    Result saveNow_(core::state::CoreState& state);

    ProductFileService& files_;
    uint32_t delay_ms_ = 0;
};

}  // namespace core::persistence

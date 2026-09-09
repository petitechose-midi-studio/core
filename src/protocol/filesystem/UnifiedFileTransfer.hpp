#pragma once

#include "UnifiedFileSystemRpc.hpp"
#include "persistence/ProductFileCommitPlan.hpp"
#include "persistence/ProductFileService.hpp"

namespace core::protocol::filesystem::unified {

/** Call only from a foreground persistence turn.
 * One active upload; completed operations occupy a bounded retention registry.
 */
class FileTransfer {
public:
    static constexpr uint8_t RETAINED_CAPACITY = 32;
    static constexpr uint32_t RETENTION_MS = 30'000;
    explicit FileTransfer(core::persistence::ProductFileService& files) : files_(files) {}
    ~FileTransfer();
    FileTransfer(const FileTransfer&) = delete;
    FileTransfer& operator=(const FileTransfer&) = delete;
    size_t process(const uint8_t* data, size_t size, uint32_t nowMs, bool playing,
                   uint8_t* output, size_t capacity);
    void advance(uint32_t nowMs, bool playing, uint8_t* scratch, size_t capacity);
private:
    Error execute(const Frame& request, uint32_t nowMs, uint8_t* body, size_t& size);
    Error begin(const Frame& request, uint32_t nowMs);
    bool release(bool discard, bool completed = false);
    bool discard(uint32_t nowMs, Error& outcome);
    bool irreversible() const;
    struct Record {
        uint32_t nonce = 0, id = 0, deadline = 0, started = 0, terminalAt = 0, media = 0;
        State state = State::Pending;
        Error error = Error::None;
    };
    Record* find(uint32_t nonce);
    Record* available();
    void expire(uint32_t nowMs);
    void terminal(State state, Error error, uint32_t nowMs);
    bool pending() const { return active_ != nullptr; }
    core::persistence::ProductFileService& files_;
    core::persistence::ProductMutationLease lease_;
    core::persistence::ProductPersistenceJobToken token_;
    core::persistence::ProductFileCommitPlan plan_;
    char final_[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1]{};
    char temporary_[64]{}, backup_[64]{};
    uint32_t session_ = 0;
    uint32_t expected_ = 0, written_ = 0, crc_ = 0, uploadStarted_ = 0;
    Record records_[RETAINED_CAPACITY]{};
    Record* active_ = nullptr;
    Error deferredError_ = Error::None;
};

}  // namespace core::protocol::filesystem::unified

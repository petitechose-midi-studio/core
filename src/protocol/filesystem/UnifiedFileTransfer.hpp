#pragma once

#include "UnifiedFileSystemRpc.hpp"
#include "persistence/ProductFileCommitPlan.hpp"
#include "persistence/ProductFileService.hpp"

namespace core::protocol::filesystem::unified {

/** Isolated vertical slice. Call only from a foreground persistence turn.
 * One upload and one retained commit; not registered in the application yet.
 */
class FileTransfer {
public:
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
    core::persistence::ProductFileService& files_;
    core::persistence::ProductMutationLease lease_;
    core::persistence::ProductPersistenceJobToken token_;
    core::persistence::ProductFileCommitPlan plan_;
    char final_[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1]{};
    char temporary_[64]{}, backup_[64]{};
    uint16_t session_ = 0;
    uint32_t expected_ = 0, written_ = 0, crc_ = 0, uploadStarted_ = 0;
    uint32_t nonce_ = 0, operationId_ = 0, commitStarted_ = 0, deadline_ = 0, terminalAt_ = 0;
    uint16_t committedSession_ = 0;
    State result_ = State::Complete;
    Error failure_ = Error::None;
};

}  // namespace core::protocol::filesystem::unified

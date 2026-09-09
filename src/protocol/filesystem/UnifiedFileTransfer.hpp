#pragma once
#include <variant>

#include "UnifiedFileSystemRpc.hpp"
#include "persistence/ProductFileCommitPlan.hpp"
#include "persistence/ProductFileService.hpp"
#include "persistence/ProductDirectoryCatalog.hpp"
#include "persistence/ProductTreeCleanupPlan.hpp"
#include "persistence/ProductConditionalMutationPlan.hpp"

namespace core::protocol::filesystem::unified {

/** Call only from a foreground persistence turn.
 * One active upload; completed operations occupy a bounded retention registry.
 */
class FileTransfer {
public:
    static constexpr uint8_t RETAINED_CAPACITY = 32;
    static constexpr uint32_t RETENTION_MS = 30'000;
    using MicrosProvider = uint32_t (*)();
    FileTransfer(core::persistence::ProductFileService& files,
                 core::persistence::ProductDirectoryCatalog& catalog, MicrosProvider micros = nullptr)
        : files_(files), catalog_(catalog), micros_(micros) {}
    ~FileTransfer();
    FileTransfer(const FileTransfer&) = delete;
    FileTransfer& operator=(const FileTransfer&) = delete;
    size_t process(const uint8_t* data, size_t size, uint32_t nowMs, bool playing,
                   uint8_t* output, size_t capacity, bool deferAdmission = false);
    void advance(uint32_t nowMs, bool playing, uint8_t* scratch, size_t capacity);
private:
    Error execute(const Frame& request, uint32_t nowMs, uint8_t* body, size_t& size,
                  core::persistence::ProductPersistenceWorkMeasurement& measurement);
    Error begin(const Frame& request, uint32_t nowMs);
    Error mutate(const Frame& request, uint32_t nowMs);
    Error beginConditional(const Frame& request, uint32_t nowMs);
    bool release(bool discard, bool completed = false);
    bool discard(uint32_t nowMs, Error& outcome);
    bool irreversible() const;
    struct Record {
        uint32_t nonce = 0, id = 0, deadline = 0, started = 0, terminalAt = 0, media = 0;
        State state = State::Pending;
        Error error = Error::None;
        Operation operation = Operation::UploadCommit;
        uint8_t fingerprint[32]{};
        uint8_t result[35]{};
        uint8_t resultSize = 0;
    };
    static_assert(sizeof(Record) <= 104, "retained result metadata grew beyond its bound");
    Record* find(uint32_t nonce);
    Record* available();
    void retain(const Frame& request, uint32_t nowMs, uint32_t identity);
    void expire(uint32_t nowMs);
    void terminal(State state, Error error, uint32_t nowMs);
    size_t respond(const Record& record, Frame response, uint8_t* output, size_t capacity);
    bool pending() const { return active_ != nullptr; }
    bool hasWork() const;
    core::persistence::ProductFileService& files_;
    core::persistence::ProductDirectoryCatalog& catalog_;
    MicrosProvider micros_;
    core::persistence::ProductMutationLease lease_;
    core::persistence::ProductPersistenceJobToken token_;
    // Only one continuation can own storage; share its memory rather than
    // retaining a separate plan buffer for each kind of mutation.
    std::variant<core::persistence::ProductFileCommitPlan,
                 core::persistence::ProductTreeCleanupPlan,
                 core::persistence::conditional_mutation::ConditionalMutationPlan> work_;
    char final_[oc::interface::FILESYSTEM_MAX_PATH_LENGTH + 1]{};
    char temporary_[64]{}, backup_[64]{};
    uint32_t session_ = 0;
    uint32_t expected_ = 0, written_ = 0, crc_ = 0, uploadStarted_ = 0;
    Record records_[RETAINED_CAPACITY]{};
    Record* active_ = nullptr;
    uint16_t traceRequest_ = 0;
    Error deferredError_ = Error::None;
};

#if defined(ARDUINO_TEENSY41) && !defined(OC_DESKTOP)
static_assert(sizeof(FileTransfer) <= 6400, "filesystem service exceeds its ARM memory budget");
#endif

}  // namespace core::protocol::filesystem::unified

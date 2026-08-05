#pragma once

#include <cstddef>
#include <cstdint>

#include "protocol/filesystem/FileSystemRpc.hpp"

namespace core::protocol::filesystem::conditional_mutation {

struct DigestReadResult {
    FileSystemRpcStatus status = FileSystemRpcStatus::STORAGE_ERROR;
    uint8_t sha256[FILESYSTEM_RPC_SHA256_SIZE] = {};
    uint32_t crc32 = 0U;
};

/**
 * Allocation-free SHA-256 continuation for one product file.
 *
 * Each advance performs either one metadata lookup or one bounded read. The
 * caller owns the scratch buffer (the RPC endpoint reuses its retained PSRAM
 * response buffer) and may therefore pause safely between every filesystem
 * primitive.
 */
class DigestReadPlan final {
public:
    void begin();
    bool advance(
        core::persistence::ProductFileService& files,
        const core::persistence::ProductMutationLease& lease,
        const char* path,
        uint8_t* scratch,
        size_t scratchSize
    );

    bool complete() const;
    bool nextAdvanceReadsData() const;
    FileSystemRpcStatus status() const { return status_; }
    uint32_t fileSize() const { return file_size_; }
    const uint8_t* digest() const { return digest_; }
    uint32_t crc32() const { return crc32_state_; }

private:
    friend bool hashBytes(const uint8_t* data, size_t size,
                          uint8_t output[FILESYSTEM_RPC_SHA256_SIZE]);

    enum class Step : uint8_t {
        IDLE = 0,
        STAT,
        READ,
        VERIFY_STAT,
        COMPLETE,
    };

    void update_(const uint8_t* data, size_t size);
    void finish_();
    void transform_(const uint8_t block[64]);
    bool fail_(FileSystemRpcStatus status);

    uint32_t state_[8] = {};
    uint8_t block_[64] = {};
    size_t block_size_ = 0U;
    uint64_t total_bytes_ = 0U;
    uint32_t file_size_ = 0U;
    uint32_t offset_ = 0U;
    uint32_t crc32_state_ = 0U;
    uint8_t digest_[FILESYSTEM_RPC_SHA256_SIZE] = {};
    FileSystemRpcStatus status_ = FileSystemRpcStatus::STORAGE_ERROR;
    Step step_ = Step::IDLE;
};

static_assert(
    sizeof(DigestReadPlan) <= 160U,
    "filesystem RPC digest continuation exceeds PSRAM control ceiling"
);

bool digestEquals(const uint8_t* lhs, const uint8_t* rhs);
void copyDigest(uint8_t* destination, const uint8_t* source);

/** Hash one already-resident byte range without allocation or filesystem I/O. */
bool hashBytes(const uint8_t* data, size_t size, uint8_t output[FILESYSTEM_RPC_SHA256_SIZE]);

DigestReadResult readDigest(
    core::persistence::ProductFileService& files,
    const core::persistence::ProductMutationLease& lease,
    const char* path
);

}  // namespace core::protocol::filesystem::conditional_mutation

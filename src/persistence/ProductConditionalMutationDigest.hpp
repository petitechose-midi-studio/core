#pragma once

#include <cstddef>
#include <cstdint>

#include "persistence/ProductConditionalMutationTransaction.hpp"

namespace core::persistence::conditional_mutation {

struct DigestReadResult {
    Status status = Status::STORAGE_ERROR;
    uint8_t sha256[SHA256_SIZE] = {};
    uint32_t crc32 = 0U;
};

/** Allocation-free SHA-256 continuation for one product file. */
class DigestReadPlan final {
public:
    void begin();
    bool advance(
        ProductFileService& files,
        const ProductMutationLease& lease,
        const char* path,
        uint8_t* scratch,
        size_t scratchSize
    );

    bool complete() const;
    bool nextAdvanceReadsData() const;
    Status status() const { return status_; }
    uint32_t fileSize() const { return file_size_; }
    const uint8_t* digest() const { return digest_; }
    uint32_t crc32() const { return crc32_state_; }

private:
    friend bool hashBytes(
        const uint8_t* data,
        size_t size,
        uint8_t output[SHA256_SIZE]
    );

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
    bool fail_(Status status);

    uint32_t state_[8] = {};
    uint8_t block_[64] = {};
    size_t block_size_ = 0U;
    uint64_t total_bytes_ = 0U;
    uint32_t file_size_ = 0U;
    uint32_t offset_ = 0U;
    uint32_t crc32_state_ = 0U;
    uint8_t digest_[SHA256_SIZE] = {};
    Status status_ = Status::STORAGE_ERROR;
    Step step_ = Step::IDLE;
};

static_assert(
    sizeof(DigestReadPlan) <= 160U,
    "conditional digest continuation exceeds PSRAM control ceiling"
);

bool digestEquals(const uint8_t* lhs, const uint8_t* rhs);
void copyDigest(uint8_t* destination, const uint8_t* source);
bool hashBytes(const uint8_t* data, size_t size, uint8_t output[SHA256_SIZE]);

DigestReadResult readDigest(
    ProductFileService& files,
    const ProductMutationLease& lease,
    const char* path
);

}  // namespace core::persistence::conditional_mutation
